"""Cut a piece of a recording for the simulator's fake sensor to play back.

    python sim/make_replay.py data/raw/P02/s10.csv --first-cue 0 --cues 20 -o /tmp/replay

Writes <out>.rows (one "ax,ay,az,gx,gy,gz" line per 10 ms sample, starting
with 2.5 s of the board held still and upright so the gyro calibration
passes) and <out>.cues (how many cues of each gesture the piece holds).
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path

STILL_ROWS = 250             # 2.5 s: calibration needs 1.5 s still
STILL = "0,-2048,0,20,-12,7"  # upright, 1 g on -y, small gyro bias (as the fake's default)
MARGIN_MS = 1500             # recording kept before the first and after the last cue


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", type=Path)
    ap.add_argument("--first-cue", type=int, default=0)
    ap.add_argument("--cues", type=int, default=20)
    ap.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args()

    labels_path = a.csv.with_name(a.csv.name.replace(".csv", ".labels.csv"))
    with open(labels_path) as f:
        cues = list(csv.DictReader(f))[a.first_cue:a.first_cue + a.cues]
    t0 = int(cues[0]["start_ms"]) - MARGIN_MS
    t1 = int(cues[-1]["end_ms"]) + MARGIN_MS

    rows = []
    with open(a.csv) as f:
        lines = (line for line in f if not line.startswith("#"))
        for r in csv.DictReader(lines):
            if t0 <= int(r["t_ms"]) <= t1:
                rows.append(",".join(r[k] for k in ("ax", "ay", "az", "gx", "gy", "gz")))

    a.out.parent.mkdir(parents=True, exist_ok=True)
    Path(f"{a.out}.rows").write_text("\n".join([STILL] * STILL_ROWS + rows) + "\n")
    counts = Counter(c["label"] for c in cues)
    Path(f"{a.out}.cues").write_text(json.dumps(counts) + "\n")
    print(f"{len(rows)} samples ({len(rows) / 100:.1f} s) after {STILL_ROWS / 100:.1f} s still; cues: {dict(counts)}")


if __name__ == "__main__":
    main()
