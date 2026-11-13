"""Train the gesture model and say how well it works on a person it never saw.

    python training/train.py                 # test on each person in turn, then
                                             # train on everyone and save the model
    python training/train.py --with-jump     # jump as a 7th class
    python training/train.py --width tiny    # smaller model

"Leave one person out": train on everybody except one person, test on that
person, repeat for each person. That is the honest number, because the real
test is a stranger picking up the board.

    python training/train.py --pool avg      # the first model's pooling (default: max)

Saves training/out/gesture_<width>.pt (weights + class list + pooling + input scaling).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import torch
from torch import nn

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dataset import SCALE, class_list, load_all  # noqa: E402
from model import POOLS, WIDTHS, GestureNet, count_parameters  # noqa: E402

OUT = Path(__file__).resolve().parent / "out"
MIN_TEST_WINDOWS = 200      # people with less data than this are train-only


def augment(x: torch.Tensor) -> torch.Tensor:
    """Small random changes so the model does not memorise two people's hands."""
    n = x.shape[0]
    x = x * (0.8 + 0.4 * torch.rand(n, 1, 1))              # stronger / weaker motion
    x = x + 0.02 * torch.randn_like(x)                     # sensor noise
    # Board held at a slightly different angle: rotate accel and gyro together
    # by up to ~15 degrees around a random axis.
    axis = torch.nn.functional.normalize(torch.randn(n, 3), dim=1)
    angle = (torch.rand(n) - 0.5) * 2 * 0.26
    K = torch.zeros(n, 3, 3)
    K[:, 0, 1], K[:, 0, 2] = -axis[:, 2], axis[:, 1]
    K[:, 1, 0], K[:, 1, 2] = axis[:, 2], -axis[:, 0]
    K[:, 2, 0], K[:, 2, 1] = -axis[:, 1], axis[:, 0]
    s, c = torch.sin(angle)[:, None, None], torch.cos(angle)[:, None, None]
    R = torch.eye(3) + s * K + (1 - c) * (K @ K)
    return torch.cat([R @ x[:, :3], R @ x[:, 3:]], dim=1)


def fit(X, y, n_classes, width, epochs, seed, pool="max"):
    torch.manual_seed(seed)
    model = GestureNet(n_classes, width, pool)
    X, y = torch.from_numpy(X), torch.from_numpy(y)

    # Rare classes count more, so "nothing" (most of the data) cannot drown them.
    counts = torch.bincount(y, minlength=n_classes).float().clamp(min=1)
    loss_fn = nn.CrossEntropyLoss(weight=(counts.sum() / counts / n_classes).clamp(max=5),
                                  label_smoothing=0.05)
    opt = torch.optim.AdamW(model.parameters(), lr=3e-3, weight_decay=1e-3)
    steps = epochs * ((len(X) + 63) // 64)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=3e-3, total_steps=steps)

    model.train()
    for _ in range(epochs):
        order = torch.randperm(len(X))
        for i in range(0, len(X), 64):
            batch = order[i:i + 64]
            if len(batch) < 2:
                continue
            loss = loss_fn(model(augment(X[batch])), y[batch])
            opt.zero_grad()
            loss.backward()
            opt.step()
            sched.step()
    return model.eval()


@torch.no_grad()
def predict(model, X):
    return model(torch.from_numpy(X)).argmax(dim=1).numpy()


def report(name, truth, guess, classes):
    n = len(classes)
    table = np.zeros((n, n), dtype=int)
    for t, g in zip(truth, guess):
        table[t, g] += 1
    recall = table.diagonal() / table.sum(axis=1).clip(min=1)
    print(f"\n{name}: {100 * (truth == guess).mean():.1f}% of windows right")
    print("  rows = what the person did, columns = what the model said")
    print("  " + " " * 11 + " ".join(f"{c[:7]:>7s}" for c in classes) + "   right")
    for i, c in enumerate(classes):
        print(f"  {c:11s}" + " ".join(f"{v:7d}" for v in table[i]) + f"   {100 * recall[i]:4.0f}%")
    return recall


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--width", choices=list(WIDTHS), default="small")
    ap.add_argument("--pool", choices=list(POOLS), default="max", help="pooling over time (see model.py)")
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--with-jump", action="store_true", help="jump as a 7th class")
    ap.add_argument("--no-save", action="store_true", help="only print the test results")
    args = ap.parse_args()

    classes = class_list(args.with_jump)
    X, y, person, _, _ = load_all(args.with_jump)
    print(f"{len(X)} windows from {', '.join(sorted(set(person)))}, classes: {', '.join(classes)}")
    print(f"model '{args.width}', {args.pool} pooling: "
          f"{count_parameters(GestureNet(len(classes), args.width, args.pool))} parameters")

    recalls = []
    for who in sorted(set(person)):
        test = person == who
        if test.sum() < MIN_TEST_WINDOWS:
            print(f"\n{who}: only {test.sum()} windows, used for training only")
            continue
        if len(set(person[~test])) == 0:
            continue
        model = fit(X[~test], y[~test], len(classes), args.width, args.epochs, args.seed, args.pool)
        recalls.append(report(f"never saw {who}", y[test], predict(model, X[test]), classes))

    if recalls:
        mean = np.mean(recalls, axis=0)
        print("\naverage over unseen people: "
              + ", ".join(f"{c} {100 * r:.0f}%" for c, r in zip(classes, mean)))

    if not args.no_save:
        model = fit(X, y, len(classes), args.width, args.epochs, args.seed, args.pool)
        OUT.mkdir(exist_ok=True)
        path = OUT / f"gesture_{args.width}.pt"
        torch.save({"state_dict": model.state_dict(), "classes": classes, "width": args.width,
                    "pool": args.pool, "input_scale": SCALE.tolist()}, path)
        print(f"\ntrained on everyone, saved {path.relative_to(Path.cwd()) if path.is_relative_to(Path.cwd()) else path}")


if __name__ == "__main__":
    main()
