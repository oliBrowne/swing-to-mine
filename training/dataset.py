"""Turn recorded sessions into 1 second training windows.

Reads data/raw/<person>/<session>.csv (+ .labels.csv) and returns

    X       float32 [N, 6, 100]   ax ay az gx gy gz, scaled (see SCALE below)
    y       int64   [N]           index into the class list
    person  str     [N]           "P01", ...
    session str     [N]           "s05", ...
    cue     int64   [N]           which cue the window came from (-1 = background)

How windows are picked:

* People react 0.3 to 2 s after the cue, so the label start time is not where
  the motion is. For each cue we look for the first sample with real motion
  (the "onset") and cut windows around that instead.
* One-shot gestures (sword, place, swaps, jump): a few windows that all contain
  the whole motion, shifted a little so the model does not care exactly where
  in the window the gesture sits.
* mine: windows all along the chopping, because the board has to keep seeing
  "mine" for as long as the person keeps chopping.
* nothing: the IDLE cues, the gaps between cues (hand drifting back to rest),
  and whole sessions without a labels file (--look-minutes recordings).

Run it directly to see the counts:   python training/dataset.py
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
from labels import CLASSES, DROPPED_OLD_NAMES, OLD_NAMES, OPTIONAL_GESTURES  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
RAW = REPO / "data" / "raw"

WINDOW = 100            # samples = 1 s at 100 Hz
CHANNELS = ["ax", "ay", "az", "gx", "gy", "gz"]

# Raw counts -> model input. The firmware must use the same numbers.
ACCEL_COUNTS_PER_G = 2048.0     # +-16 g range
GYRO_COUNTS_PER_DPS = 16.4      # +-2000 dps range
GYRO_UNIT_DPS = 250.0           # 250 deg/s becomes 1.0
SCALE = np.array([1 / ACCEL_COUNTS_PER_G] * 3
                 + [1 / (GYRO_COUNTS_PER_DPS * GYRO_UNIT_DPS)] * 3, dtype=np.float32)

# "Real motion" = turning faster than this, or shaking harder than this.
ONSET_GYRO_DPS = 120.0
ONSET_ACCEL_G = 0.45

# Cues where the person did nothing or did the wrong gesture (1-based cue
# number in the labels file), found by looking at the data on Nov 13.
BAD_CUES = {
    ("P01", "s01"): {3},
    ("P01", "s06"): {61},
    ("P02", "s02"): {25, 37},
    ("P02", "s09"): {25},
    ("P02", "s15"): {12},
}

# Where each window starts, in samples relative to the motion onset.
ONE_SHOT_SHIFTS = (-35, -27, -20, -12, -5)
MINE_SHIFTS = (-20, -5, 10, 25, 40, 55, 70, 85, 100)


def class_list(with_jump: bool = False) -> list[str]:
    return CLASSES + (OPTIONAL_GESTURES if with_jump else [])


def load_session(csv_path: Path):
    """Returns (t_ms, scaled [T, 6], motion mask [T])."""
    d = pd.read_csv(csv_path, comment="#")
    t = d["t_ms"].to_numpy(dtype=np.int64)
    v = d[CHANNELS].to_numpy(dtype=np.float32) * SCALE

    gyro_dps = np.linalg.norm(v[:, 3:], axis=1) * GYRO_UNIT_DPS
    accel = v[:, :3]
    kernel = np.ones(50) / 50                       # 0.5 s average = gravity
    slow = np.stack([np.convolve(accel[:, i], kernel, "same") for i in range(3)], axis=1)
    shake_g = np.linalg.norm(accel - slow, axis=1)
    moving = (gyro_dps > ONSET_GYRO_DPS) | (shake_g > ONSET_ACCEL_G)
    return t, v, moving


def _window(t, v, start):
    """One [6, 100] window, or None if it runs off the file or has a time gap."""
    if start < 0 or start + WINDOW > len(v):
        return None
    if t[start + WINDOW - 1] - t[start] > 1100:     # dropped samples / clock jump
        return None
    return v[start:start + WINDOW].T


def session_windows(csv_path: Path, with_jump: bool = False):
    """Yields (window, class_index, cue_number) for one session."""
    classes = class_list(with_jump)
    person, session = csv_path.parent.name, csv_path.stem
    t, v, moving = load_session(csv_path)

    labels_path = csv_path.with_name(session + ".labels.csv")
    labels = pd.read_csv(labels_path) if labels_path.exists() else pd.DataFrame(
        columns=["start_ms", "end_ms", "label"])

    # No cues at all = a look-around recording: everything is "nothing".
    if len(labels) == 0:
        for start in range(0, len(v) - WINDOW, 25):
            w = _window(t, v, start)
            if w is not None:
                yield w, 0, -1
        return

    bad = BAD_CUES.get((person, session), set())
    starts = labels["start_ms"].tolist() + [int(t[-1])]
    busy = np.zeros(len(t), dtype=bool)             # near any cue

    for i, row in labels.iterrows():
        label = OLD_NAMES.get(row["label"], row["label"])
        cue_end = min(starts[i + 1], row["start_ms"] + 4500)
        busy[np.searchsorted(t, row["start_ms"] - 200):np.searchsorted(t, cue_end)] = True

        if (i + 1) in bad or label in DROPPED_OLD_NAMES or label not in classes:
            continue
        index = classes.index(label)

        if label == "nothing":
            first = np.searchsorted(t, row["start_ms"] + 300)
            for start in range(first, first + 151, 50):
                w = _window(t, v, start)
                if w is not None:
                    yield w, 0, i + 1
            continue

        lo = np.searchsorted(t, row["start_ms"] + 200)
        hi = np.searchsorted(t, min(starts[i + 1], row["start_ms"] + 3500))
        active = np.flatnonzero(moving[lo:hi])
        if len(active) == 0:                        # no motion found: skip the cue
            continue
        onset = lo + active[0]

        if label == "mine":
            last = lo + active[-1]                  # where the chopping stops
            for shift in MINE_SHIFTS:
                start = onset + shift
                if start + WINDOW - 30 > last:      # mostly past the chopping
                    break
                w = _window(t, v, start)
                if w is not None:
                    yield w, index, i + 1
        else:
            for shift in ONE_SHOT_SHIFTS:
                w = _window(t, v, onset + shift)
                if w is not None:
                    yield w, index, i + 1

    # Gaps between cues: the hand drifting back to rest, fidgeting.
    for start in range(0, len(v) - WINDOW, 50):
        if not busy[start:start + WINDOW].any():
            w = _window(t, v, start)
            if w is not None:
                yield w, 0, -1


def load_all(with_jump: bool = False, raw_dir: Path = RAW):
    X, y, person, session, cue = [], [], [], [], []
    for csv_path in sorted(raw_dir.glob("P*/s*.csv")):
        if csv_path.name.endswith(".labels.csv"):
            continue
        for w, index, cue_number in session_windows(csv_path, with_jump):
            X.append(w)
            y.append(index)
            person.append(csv_path.parent.name)
            session.append(csv_path.stem)
            cue.append(cue_number)
    return (np.stack(X).astype(np.float32), np.array(y, dtype=np.int64),
            np.array(person), np.array(session), np.array(cue, dtype=np.int64))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--with-jump", action="store_true", help="count jump as a class")
    args = ap.parse_args()

    classes = class_list(args.with_jump)
    X, y, person, session, cue = load_all(args.with_jump)
    print(f"{len(X)} windows, shape {X.shape[1:]}")
    table = pd.crosstab(pd.Series(person, name="person"),
                        pd.Series([classes[i] for i in y], name="class"))
    print(table[[c for c in classes if c in table.columns]].to_string())


if __name__ == "__main__":
    main()
