"""Channel pruning for the gesture model (the Lab 3 idea, applied to Swing to Mine).

    python training/prune.py                       # full test: every person left out in turn
    python training/prune.py --seeds 0 1 2         # repeat with 3 training seeds
    python training/prune.py --save bn 0.5 --finetune-epochs 30
                                                   # prune the everyone-model, save it for export.py

What it does, for each person the model never saw (leave one person out):

  1. train the normal `small` model (16/24/32 channels) on everyone else (train.py's fit)
  2. score every output channel of the three convolutions with a criterion
       bn      |batch-norm scale| of the channel (best in Lab 3)
       l1      L1 norm of the channel's conv weights
       random  a random pick (control: is the criterion doing anything?)
  3. keep the best `keep` fraction of channels in every layer, cut the rest
     (and the matching inputs of the next layer and of the head)
  4. fine-tune the smaller model for 10 epochs at a lower learning rate
     (--finetune-epochs 30 gives it the same training budget as `scratch`)
  5. score it on the unseen person: float, int8 (post-training quantization with
     the board's CortexMQuantizer, calibrated on the training people only), and
     the whole-recording replay with the board's trigger rules
  and, as the honest baseline, `scratch`: the same small widths trained from
  zero for 30 epochs, no pruning.

Writes one JSON line per run to training/out/prune_runs.jsonl and prints a
summary table. int8 needs the ExecuTorch env (see firmware/swing_to_mine/README.md);
without it, use --no-int8.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections import Counter, defaultdict
from multiprocessing import Pool
from pathlib import Path

import numpy as np
import torch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from dataset import RAW, SCALE, class_list, load_all, load_session  # noqa: E402
from evaluate_stream import decode, score_session, summary  # noqa: E402
from model import WIDTHS, GestureNet, count_macs, count_parameters  # noqa: E402
from train import MIN_TEST_WINDOWS, fit  # noqa: E402

OUT = HERE / "out"
KEEPS = (1.0, 0.75, 0.5, 0.35, 0.25)
CRITERIA = ("bn", "l1", "random")
EPOCHS, FINETUNE_EPOCHS, FINETUNE_LR = 30, 10, 1e-3
STRIDE, WINDOW = 5, 100


def channel_scores(net: GestureNet, criterion: str, seed: int) -> list[np.ndarray]:
    f = net.features
    convs, bns = [f[i] for i in range(0, len(f), 3)], [f[i + 1] for i in range(0, len(f), 3)]
    if criterion == "bn":
        return [bn.weight.detach().abs().numpy() for bn in bns]
    if criterion == "l1":
        return [c.weight.detach().abs().sum(dim=(1, 2)).numpy() for c in convs]
    rng = np.random.default_rng(seed + 1234)
    return [rng.random(c.out_channels) for c in convs]


def kept_widths(widths, keep: float) -> tuple[int, int, int]:
    return tuple(max(2, int(round(c * keep))) for c in widths)


def prune(net: GestureNet, criterion: str, keep: float, seed: int = 0) -> GestureNet:
    """A new, smaller GestureNet holding the kept channels' weights."""
    widths = kept_widths(net.widths, keep)
    scores = channel_scores(net, criterion, seed)
    keep_idx = [np.sort(np.argsort(-s)[:w]) for s, w in zip(scores, widths)]
    small = GestureNet(net.head.out_features, widths, net.pool)
    old, new = net.features, small.features
    prev = np.arange(6)
    for layer, idx in enumerate(keep_idx):
        i = 3 * layer
        out_i, in_i = torch.from_numpy(idx), torch.from_numpy(prev)
        new[i].weight.data = old[i].weight.data[out_i][:, in_i].clone()
        for name in ("weight", "bias", "running_mean", "running_var"):
            getattr(new[i + 1], name).data = getattr(old[i + 1], name).data[out_i].clone()
        prev = idx
    small.head.weight.data = net.head.weight.data[:, torch.from_numpy(prev)].clone()
    small.head.bias.data = net.head.bias.data.clone()
    return small.eval()


def int8_model(net: GestureNet, X_calib: np.ndarray, y_calib: np.ndarray):
    """Post-training int8, the same quantizer and graph as export.py. Returns a
    function [N, 6, 100] -> scores that runs the quantized (simulated) model."""
    from executorch.backends.cortex_m.quantizer.quantizer import CortexMQuantizer
    from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e, prepare_pt2e

    from export import GestureNet2d, to2d

    m2 = GestureNet2d(net).eval()
    Xt = torch.from_numpy(X_calib)
    gm = prepare_pt2e(torch.export.export(m2, (to2d(Xt[:1]),)).module(), CortexMQuantizer())
    rng = np.random.default_rng(0)        # same calibration pick as export.py
    calib = np.unique(np.concatenate([rng.choice(len(Xt), min(600, len(Xt)), replace=False)]
                                     + [np.flatnonzero(y_calib == c)[:40] for c in range(net.head.out_features)]))
    with torch.no_grad():
        for i in range(0, len(calib), 32):
            gm(to2d(Xt[calib[i:i + 32]]))
    gm = convert_pt2e(gm)

    def run(x):
        with torch.no_grad():
            return torch.cat([gm(to2d(torch.from_numpy(x[i:i + 512])))
                              for i in range(0, len(x), 512)]).numpy()
    return run


def float_model(net):
    def run(x):
        with torch.no_grad():
            return torch.cat([net(torch.from_numpy(x[i:i + 2048])) for i in range(0, len(x), 2048)]).numpy()
    return run


def replay(run, who, classes):
    """Whole-recording replay with the board's trigger rules (evaluate_stream.py)."""
    total = defaultdict(Counter)
    for csv_path in sorted((RAW / who).glob("s*.csv")):
        if csv_path.name.endswith(".labels.csv"):
            continue
        t, v, _ = load_session(csv_path)
        starts = np.arange(0, len(v) - WINDOW, STRIDE)
        starts = starts[(t[starts + WINDOW - 1] - t[starts]) <= 1100]
        x = np.stack([v[s:s + WINDOW].T for s in starts]).astype(np.float32)
        probs = torch.softmax(torch.from_numpy(run(x)), dim=1).numpy()
        events, holds = decode(t[starts + WINDOW - 1], probs, classes)
        for key, counts in score_session(csv_path, events, holds, classes).items():
            total[key].update(counts)
    hit, extra, false_per_min = summary(total, classes)
    gestures = [c for c in classes if c != "nothing"]
    return {"replay_hit": hit, "replay_false_min": false_per_min, "replay_extra": extra,
            "replay_cues": sum(total[g]["cues"] for g in gestures),
            "replay_hits": sum(total[g]["hit"] for g in gestures),
            "idle_false": total["_idle"]["false"], "idle_ms": total["_idle"]["ms"]}


def scores_of(run, X, y, n):
    guess = run(X).argmax(1)
    recall = [float((guess[y == c] == c).mean()) if (y == c).any() else float("nan") for c in range(n)]
    return float((guess == y).mean()), recall, guess


def one_fold(job):
    who, seed, do_int8, ft_epochs = job
    torch.set_num_threads(1)
    classes = class_list(False)
    n = len(classes)
    X, y, person, _, _ = load_all(False)
    test = person == who
    Xtr, ytr, Xte, yte = X[~test], y[~test], X[test], y[test]
    rows = []

    def evaluate(net, method, keep, train_s):
        row = {"who": who, "seed": seed, "method": method, "keep": keep, "widths": list(net.widths),
               "params": count_parameters(net), "macs": count_macs(net.widths, n), "train_s": round(train_s, 1)}
        acc, rec, fg = scores_of(float_model(net), Xte, yte, n)
        row.update(float_acc=acc, float_recall=rec, **{"float_" + k: v for k, v in
                                                       replay(float_model(net), who, classes).items()})
        if do_int8:
            q = int8_model(net, Xtr, ytr)
            acc8, rec8, qg = scores_of(q, Xte, yte, n)
            row.update(int8_acc=acc8, int8_recall=rec8, int8_same=float((qg == fg).mean()),
                       **{"int8_" + k: v for k, v in replay(q, who, classes).items()})
        rows.append(row)
        print(f"  {who} seed {seed} {method:7s} keep {keep:.2f} {str(row['widths']):13s} "
              f"float {100 * acc:5.1f}%" + (f" int8 {100 * row['int8_acc']:5.1f}%" if do_int8 else "")
              + f" replay {row['float_replay_hit']:5.1f}%", flush=True)

    t0 = time.time()
    base = fit(Xtr, ytr, n, "small", EPOCHS, seed)
    base_s = time.time() - t0
    evaluate(base, "full", 1.0, base_s)
    for keep in KEEPS[1:]:
        for crit in CRITERIA:
            t0 = time.time()
            small = fit(Xtr, ytr, n, None, ft_epochs, seed, model=prune(base, crit, keep, seed),
                        lr=FINETUNE_LR)
            evaluate(small, crit, keep, time.time() - t0)
        t0 = time.time()
        scratch = fit(Xtr, ytr, n, kept_widths(WIDTHS["small"], keep), EPOCHS, seed)
        evaluate(scratch, "scratch", keep, time.time() - t0)
    return rows


def summarize(rows, classes):
    by = defaultdict(list)
    for r in rows:
        by[(r["method"], r["keep"])].append(r)
    have8 = all("int8_acc" in r for r in rows)
    print(f"\n{len({r['who'] for r in rows})} unseen people x {len({r['seed'] for r in rows})} seeds, "
          "mean over people and seeds")
    head = f"{'method':8s} {'keep':>5s} {'widths':>12s} {'params':>6s} {'MACs':>7s} {'float':>6s}"
    head += f" {'int8':>6s} {'same':>6s}" if have8 else ""
    print(head + f" {'replay':>7s} {'false/min':>9s}  weakest class (float)")
    for (method, keep), rs in sorted(by.items(), key=lambda kv: (-kv[0][1], kv[0][0])):
        m = lambda k: np.mean([r[k] for r in rs])  # noqa: E731
        rec = np.nanmean([r["float_recall"] for r in rs], axis=0)
        weak = int(np.nanargmin(rec))
        idle_false = sum(r["float_idle_false"] for r in rs) / max(sum(r["float_idle_ms"] for r in rs) / 60000, 1e-9)
        hit = 100 * sum(r["float_replay_hits"] for r in rs) / max(sum(r["float_replay_cues"] for r in rs), 1)
        line = (f"{method:8s} {keep:5.2f} {str(tuple(rs[0]['widths'])):>12s} {rs[0]['params']:6d} "
                f"{rs[0]['macs']:7d} {100 * m('float_acc'):5.1f}%")
        if have8:
            line += f" {100 * m('int8_acc'):5.1f}% {100 * m('int8_same'):5.1f}%"
        print(line + f" {hit:6.1f}% {idle_false:9.2f}  {classes[weak]} {100 * rec[weak]:.0f}%")


def save_pruned(criterion, keep, seed, ft_epochs=FINETUNE_EPOCHS):
    """Prune the everyone-model the same way and save it for export.py."""
    classes = class_list(False)
    X, y, *_ = load_all(False)
    base = fit(X, y, len(classes), "small", EPOCHS, seed)
    net = base if keep >= 1 else fit(X, y, len(classes), None, ft_epochs, seed,
                                     model=prune(base, criterion, keep, seed), lr=FINETUNE_LR)
    name = f"gesture_{criterion}{int(round(100 * keep))}.pt" if keep < 1 else "gesture_full.pt"
    OUT.mkdir(exist_ok=True)
    torch.save({"state_dict": net.state_dict(), "classes": classes, "width": list(net.widths),
                "pool": net.pool, "input_scale": SCALE.tolist(), "pruned": {"criterion": criterion, "keep": keep}},
               OUT / name)
    print(f"saved {OUT / name}: widths {net.widths}, {count_parameters(net)} parameters, "
          f"{count_macs(net.widths, len(classes))} multiply-adds")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seeds", type=int, nargs="+", default=[0])
    ap.add_argument("--jobs", type=int, default=4, help="processes (one fold each)")
    ap.add_argument("--no-int8", action="store_true", help="skip int8 (no ExecuTorch env needed)")
    ap.add_argument("--save", nargs=2, metavar=("CRITERION", "KEEP"),
                    help="prune the everyone-model and save it, e.g. --save bn 0.5")
    ap.add_argument("--finetune-epochs", type=int, default=FINETUNE_EPOCHS)
    ap.add_argument("--log", type=Path, default=OUT / "prune_runs.jsonl", help="results file (appended)")
    ap.add_argument("--summary", action="store_true", help="only summarize the results file")
    args = ap.parse_args()
    classes = class_list(False)
    log = args.log

    if args.save:
        save_pruned(args.save[0], float(args.save[1]), args.seeds[0], args.finetune_epochs)
        return
    if not args.summary:
        _, _, person, _, _ = load_all(False)
        people = [p for p in sorted(set(person)) if (person == p).sum() >= MIN_TEST_WINDOWS]
        jobs = [(p, s, not args.no_int8, args.finetune_epochs) for s in args.seeds for p in people]
        print(f"{len(jobs)} folds ({', '.join(people)}; seeds {args.seeds}), {args.jobs} at a time")
        OUT.mkdir(exist_ok=True)
        with Pool(args.jobs) as pool, open(log, "a") as f:
            for rows in pool.imap_unordered(one_fold, jobs):
                for r in rows:
                    f.write(json.dumps(r) + "\n")
                f.flush()
    rows = [json.loads(line) for line in open(log)]
    summarize(rows, classes)


if __name__ == "__main__":
    main()
