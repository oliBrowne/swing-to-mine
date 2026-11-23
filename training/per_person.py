"""Leave-one-person-out results, one row per person, averaged over a few seeds.

    python training/per_person.py                # seeds 0-4, 4 processes (~6 min on 4 cores)
    python training/per_person.py --seeds 0 1 --jobs 2

For each person: train on everyone else (train.fit, default settings), then
  - score that person's cut-out 1 s windows (like train.py), and
  - replay their whole recordings through the decoder (like evaluate_stream.py):
    share of cued gestures where the right one fired, and accidental gestures
    per minute between cues.
People with fewer than MIN_TEST_WINDOWS windows are used for training only.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter, defaultdict
from multiprocessing import Pool
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dataset import RAW, class_list, load_all, load_session  # noqa: E402
from evaluate_stream import decode, score_session, stream_probabilities  # noqa: E402
from train import MIN_TEST_WINDOWS, fit, predict  # noqa: E402

CLASSES = class_list(False)
_DATA = None


def run(job):
    """Train without one person, test on them. Returns plain numbers."""
    global _DATA
    seed, who = job
    torch.set_num_threads(1)
    if _DATA is None:
        _DATA = load_all(False)
    X, y, person, _, _ = _DATA
    test = person == who
    model = fit(X[~test], y[~test], len(CLASSES), "small", 30, seed, "max")

    n = len(CLASSES)
    table = np.zeros((n, n), dtype=int)
    for truth, guess in zip(y[test], predict(model, X[test])):
        table[truth, guess] += 1

    total = defaultdict(Counter)
    for csv_path in sorted((RAW / who).glob("s*.csv")):
        if csv_path.name.endswith(".labels.csv"):
            continue
        t, v, _ = load_session(csv_path)
        times, probs = stream_probabilities(model, t, v)
        events, holds = decode(times, probs, CLASSES)
        for key, counts in score_session(csv_path, events, holds, CLASSES).items():
            total[key].update(counts)
    gestures = [c for c in CLASSES if c != "nothing"]
    return dict(seed=seed, who=who, table=table,
                cues=sum(total[g]["cues"] for g in gestures),
                hits=sum(total[g]["hit"] for g in gestures),
                false=total["_idle"]["false"], idle_min=total["_idle"]["ms"] / 60000)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seeds", type=int, nargs="+", default=[0, 1, 2, 3, 4])
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args()

    _, _, person, _, _ = load_all(False)
    people = [p for p in sorted(set(person)) if (person == p).sum() >= MIN_TEST_WINDOWS]
    jobs = [(s, p) for s in args.seeds for p in people]
    with Pool(args.jobs) as pool:
        results = pool.map(run, jobs)

    print(f"seeds {args.seeds}, mean over seeds (min-max)\n")
    print(f"{'person':6s} {'cues':>5s} {'windows right':>17s}  {'weakest gesture':16s} "
          f"{'replay: cues caught':>20s} {'false/min':>16s}")
    recalls = []
    for who in people:
        rs = [r for r in results if r["who"] == who]
        tables = np.array([r["table"] for r in rs])
        acc = 100 * tables.trace(axis1=1, axis2=2) / tables.sum(axis=(1, 2))
        recall = 100 * (tables.diagonal(axis1=1, axis2=2) / tables.sum(axis=2).clip(min=1)).mean(axis=0)
        recalls.append(recall)
        weak = 1 + int(np.argmin(recall[1:]))
        hit = np.array([100 * r["hits"] / max(r["cues"], 1) for r in rs])
        fpm = np.array([r["false"] / max(r["idle_min"], 1e-9) for r in rs])
        print(f"{who:6s} {rs[0]['cues']:5d} {acc.mean():5.1f}% ({acc.min():.1f}-{acc.max():.1f})  "
              f"{CLASSES[weak]:10s} {recall[weak]:3.0f}% {hit.mean():8.1f}% ({hit.min():.0f}-{hit.max():.0f}) "
              f"{fpm.mean():6.2f} ({fpm.min():.2f}-{fpm.max():.2f})")

    print("\nper gesture, windows right, mean over people: "
          + ", ".join(f"{c} {r:.1f}%" for c, r in zip(CLASSES, np.mean(recalls, axis=0))))
    for s in args.seeds:
        rs = [r for r in results if r["seed"] == s]
        cues, hits = sum(r["cues"] for r in rs), sum(r["hits"] for r in rs)
        false, minutes = sum(r["false"] for r in rs), sum(r["idle_min"] for r in rs)
        print(f"seed {s}: all people pooled, cues caught {100 * hits / cues:.1f}% of {cues}, "
              f"false {false} in {minutes:.1f} min = {false / minutes:.2f}/min")


if __name__ == "__main__":
    main()
