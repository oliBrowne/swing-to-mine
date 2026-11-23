"""What the PC says should fire for a replay piece (sim/make_replay.py):
the float model from training/out/gesture_small.pt plus decode() from
training/evaluate_stream.py, over exactly the rows the fake sensor plays.

    python sim/expected_events.py /tmp/replay.rows      # prints JSON counts

Needs torch (the training env). The firmware runs the int8 version of the same
model, so a small difference in counts is possible but should be rare.
"""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path

import numpy as np

TRAINING = Path(__file__).resolve().parents[3] / "training"
sys.path.insert(0, str(TRAINING))
from dataset import SCALE  # noqa: E402
from evaluate_stream import decode, stream_probabilities  # noqa: E402
from model import load_checkpoint  # noqa: E402


def main() -> None:
    rows = np.loadtxt(sys.argv[1], delimiter=",", dtype=np.float32)
    v = rows * SCALE
    t = np.arange(len(v), dtype=np.int64) * 10          # the fake plays one row per 10 ms
    model, ck = load_checkpoint(TRAINING / "out" / "gesture_small.pt")
    times, probs = stream_probabilities(model, t, v)
    events, holds = decode(times, probs, ck["classes"])
    print(json.dumps(Counter(name for _, name in events)))


if __name__ == "__main__":
    main()
