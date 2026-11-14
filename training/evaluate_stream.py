"""Replay whole recordings through the model the way the board would.

    python training/evaluate_stream.py
    python training/evaluate_stream.py --threshold 0.8 --in-a-row 2

train.py scores cut-out one second windows. The board never sees those: it
sees one long stream, runs the model every 50 ms and has to decide when a
gesture happened. This script does the same on a person the model never saw:

  1. train on everybody else (same as train.py)
  2. slide a 1 s window over each of that person's sessions, every 50 ms
  3. run the same decoder as firmware/live_test (see decode below)
  4. compare what fired with what was cued

Per cued gesture:
  hit     the right gesture fired during the cue
  miss    nothing right fired
  double  the right gesture fired more than once
  extra   other gestures that fired during the cue (a sword that also sends a
          hotbar scroll is a bad sword, even if the sword fired too)
False triggers: anything that fires during IDLE cues or between cues, per minute.
Jump cues are ignored (jump is not a model class by default).
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np
import pandas as pd
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dataset import BAD_CUES, RAW, WINDOW, class_list, load_all, load_session  # noqa: E402
from labels import DROPPED_OLD_NAMES, OLD_NAMES  # noqa: E402
from train import MIN_TEST_WINDOWS, fit  # noqa: E402

STRIDE = 5                  # samples = 50 ms, like the board
# Decoder settings (keep in step with firmware/live_test/src/main.cpp)
THRESHOLD = 0.60            # model confidence needed
IN_A_ROW = 3                # runs in a row (x 50 ms)
COOLDOWN_MS = 300           # after any gesture fires
REARM_RUNS = 3              # runs a gesture must stop winning before it can fire again
MINE_RELEASE_MS = 400       # mine lets go after this long without winning
MIN_HOLD_MS = 500           # scoring: a mine cue counts if mine was held this long during it
CUE_SPAN_MS = 4500          # a cue "owns" this long, or until the next cue


@torch.no_grad()
def stream_probabilities(model, t, v):
    """Model confidence at every 50 ms step. Returns (end time of window, probs)."""
    starts = np.arange(0, len(v) - WINDOW, STRIDE)
    ok = (t[starts + WINDOW - 1] - t[starts]) <= 1100          # skip time gaps
    starts = starts[ok]
    x = np.stack([v[s:s + WINDOW].T for s in starts]).astype(np.float32)
    probs = torch.cat([torch.softmax(model(torch.from_numpy(x[i:i + 2048])), dim=1)
                       for i in range(0, len(x), 2048)]).numpy()
    return t[starts + WINDOW - 1], probs


def decode(times, probs, classes, threshold=THRESHOLD, in_a_row=IN_A_ROW,
           cooldown_ms=COOLDOWN_MS, rearm=REARM_RUNS, mine_release_ms=MINE_RELEASE_MS):
    """The decoder. firmware/live_test/src/main.cpp decode() must do the same.

    Every 50 ms the model names a winner. If its confidence is under the
    threshold, the winner counts as "nothing".
      * A gesture fires when it has won `in_a_row` or more runs in a row, no
        cooldown is active and it is not blocked.
      * After firing, that gesture is blocked until the model has named
        something else `rearm` runs in a row. One swing stays inside the 1 s
        window for a while; without this it fires twice.
      * mine is a hold: it starts like the others and lets go once mine has
        not been the model's top pick for `mine_release_ms`.
    Returns (events, holds): events is a list of (time_ms, class name), for
    mine the time the hold started; holds is a list of (start_ms, end_ms) during
    which mine was held.
    """
    mine = classes.index("mine")
    events, holds, streak_class, streak, quiet_until = [], [], -1, 0, -1
    mining, mine_last, mine_start = False, -1, -1
    blocked, lost = -1, 0
    for now, p in zip(times, probs):
        raw = int(p.argmax())
        top = raw if p[raw] >= threshold else 0
        streak = streak + 1 if top == streak_class else 1
        streak_class = top

        if blocked >= 0:
            lost = lost + 1 if raw != blocked else 0
            if lost >= rearm:
                blocked = -1
        if mining:
            if raw == mine:
                mine_last = now
            elif now - mine_last > mine_release_ms:
                mining = False
                holds.append((mine_start, int(now)))
                quiet_until = now + cooldown_ms
            continue
        if top != 0 and streak >= in_a_row and now >= quiet_until and top != blocked:
            events.append((int(now), classes[top]))
            if top == mine:
                mining, mine_last, mine_start = True, now, int(now)
            else:
                quiet_until = now + cooldown_ms
                if rearm > 0:
                    blocked, lost = top, 0
    if mining:
        holds.append((mine_start, int(times[-1])))
    return events, holds


def score_session(csv_path, events, holds, classes):
    """Counts for one session: per-gesture hit/miss/extra and false triggers."""
    person, session = csv_path.parent.name, csv_path.stem
    t, _, _ = load_session(csv_path)
    labels_path = csv_path.with_name(session + ".labels.csv")
    labels = pd.read_csv(labels_path) if labels_path.exists() else pd.DataFrame(
        columns=["start_ms", "end_ms", "label"])
    bad = BAD_CUES.get((person, session), set())

    out = defaultdict(Counter)
    owned = np.zeros(len(events), dtype=bool)       # event falls inside a gesture cue
    ignore_ms = 0
    starts = labels["start_ms"].tolist() + [int(t[-1])]
    for i, row in labels.iterrows():
        label = OLD_NAMES.get(row["label"], row["label"])
        lo, hi = row["start_ms"], min(starts[i + 1], row["start_ms"] + CUE_SPAN_MS)
        inside = [k for k, (when, _) in enumerate(events) if lo <= when < hi]
        skip = (i + 1) in bad or label in DROPPED_OLD_NAMES or label not in classes
        if label == "nothing" and not skip:
            continue                                # counted as idle time below
        owned[inside] = True
        ignore_ms += hi - lo
        if skip:
            continue
        fired = [events[k][1] for k in inside]
        # A hold that started in the previous cue and is still on counts: the
        # game would still be mining.
        if label == "mine" and "mine" not in fired and any(
                min(b, hi) - max(a, lo) >= MIN_HOLD_MS for a, b in holds):
            fired.append("mine")
        out[label]["cues"] += 1
        out[label]["hit" if label in fired else "miss"] += 1
        out[label]["double"] += max(fired.count(label) - 1, 0)
        for name in fired:
            if name != label:
                out[label]["extra"] += 1
                out[label]["extra:" + name] += 1

    idle_ms = max(int(t[-1] - t[0]) - ignore_ms, 0)
    for k, (_, name) in enumerate(events):
        if not owned[k]:
            out["_idle"]["false"] += 1
            out["_idle"]["false:" + name] += 1
    out["_idle"]["ms"] += idle_ms
    return out


def evaluate(threshold, in_a_row, width="small", epochs=30, seed=0, pool="max", quiet=False):
    classes = class_list(False)
    X, y, person, _, _ = load_all(False)
    total = defaultdict(Counter)
    for who in sorted(set(person)):
        test = person == who
        if test.sum() < MIN_TEST_WINDOWS:
            continue
        model = fit(X[~test], y[~test], len(classes), width, epochs, seed, pool)
        for csv_path in sorted((RAW / who).glob("s*.csv")):
            if csv_path.name.endswith(".labels.csv"):
                continue
            t, v, _ = load_session(csv_path)
            times, probs = stream_probabilities(model, t, v)
            events, holds = decode(times, probs, classes, threshold, in_a_row)
            for key, counts in score_session(csv_path, events, holds, classes).items():
                total[key].update(counts)
                total[who + "/" + key].update(counts)
    if not quiet:
        report(total, classes)
    return total


def summary(total, classes):
    """(hit %, extra per 100 gestures, false triggers per minute)"""
    gestures = [c for c in classes if c != "nothing"]
    cues = sum(total[g]["cues"] for g in gestures)
    hits = sum(total[g]["hit"] for g in gestures)
    extra = sum(total[g]["extra"] for g in gestures)
    minutes = total["_idle"]["ms"] / 60000
    return 100 * hits / max(cues, 1), 100 * extra / max(cues, 1), total["_idle"]["false"] / max(minutes, 1e-9)


def report(total, classes):
    print(f"\n{'gesture':11s} {'cues':>5s} {'hit':>6s} {'miss':>5s} {'extra':>6s} {'double':>7s}  what else fired")
    for g in classes:
        if g == "nothing":
            continue
        c = total[g]
        other = ", ".join(f"{k[6:]} x{n}" for k, n in sorted(c.items()) if k.startswith("extra:"))
        print(f"{g:11s} {c['cues']:5d} {100 * c['hit'] / max(c['cues'], 1):5.1f}% {c['miss']:5d} "
              f"{c['extra']:6d} {c['double']:7d}  {other}")
    hit, extra, false_per_min = summary(total, classes)
    idle = total["_idle"]
    what = ", ".join(f"{k[6:]} x{n}" for k, n in sorted(idle.items()) if k.startswith("false:"))
    print(f"\nright gesture fired: {hit:.1f}% of cues   wrong gesture also fired: {extra:.1f} per 100 cues")
    print(f"false triggers while idle or between cues: {idle['false']} in {idle['ms'] / 60000:.1f} min "
          f"= {false_per_min:.2f} per minute  ({what})")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--threshold", type=float, default=THRESHOLD)
    ap.add_argument("--in-a-row", type=int, default=IN_A_ROW)
    ap.add_argument("--width", default="small")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    print(f"decoder: confidence >= {args.threshold} on {args.in_a_row} runs in a row, "
          f"{COOLDOWN_MS} ms cooldown, re-arm after {REARM_RUNS} runs")
    evaluate(args.threshold, args.in_a_row, args.width, seed=args.seed)


if __name__ == "__main__":
    main()
