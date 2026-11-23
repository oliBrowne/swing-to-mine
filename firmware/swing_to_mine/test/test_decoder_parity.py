"""Checks that the firmware's gesture decoder (src/gesture_decoder.c) fires
exactly the same events as decode() in training/evaluate_stream.py.

    make -C firmware/swing_to_mine/test parity

Feeds both the same random model outputs (winners that tend to repeat, like
the real model's) and compares every event and mine hold. Only needs numpy:
decode() and its settings are read out of evaluate_stream.py without
importing torch.
"""
import ast
import ctypes
import random
import re
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
SRC = HERE.parents[2] / "training" / "evaluate_stream.py"
sys.path.insert(0, str(HERE.parents[2] / "training"))
from labels import CLASSES  # noqa: E402

# pull decode() and the UPPER_CASE settings out of evaluate_stream.py
tree = ast.parse(SRC.read_text())
keep = [n for n in tree.body
        if (isinstance(n, ast.FunctionDef) and n.name == "decode")
        or (isinstance(n, ast.Assign) and all(isinstance(t, ast.Name) and t.id.isupper() for t in n.targets))]
ns = {"np": np}
exec(compile(ast.Module(body=keep, type_ignores=[]), str(SRC), "exec"), ns)
decode = ns["decode"]

lib = ctypes.CDLL(str(HERE / "libdecoder.so"))


class Decoder(ctypes.Structure):
    _fields_ = [("streak_class", ctypes.c_int), ("streak", ctypes.c_int), ("mining", ctypes.c_int),
                ("cooldown_until", ctypes.c_uint32), ("mine_last", ctypes.c_uint32),
                ("blocked", ctypes.c_int), ("lost", ctypes.c_int)]


lib.decoder_update.argtypes = [ctypes.POINTER(Decoder), ctypes.c_int, ctypes.c_float, ctypes.c_uint32]
lib.decoder_update.restype = ctypes.c_int
GEV = {1: "sword", 2: "place", 3: "swap_left", 4: "swap_right", 5: "mine"}   # 6 = mine stop

# the C file's own copy of the settings must match too
c_src = (HERE.parent / "src" / "gesture_decoder.h").read_text()
for c_name, py_name in [("DEC_MIN_PROB", "THRESHOLD"), ("DEC_AGREE_RUNS", "IN_A_ROW"),
                        ("DEC_COOLDOWN_MS", "COOLDOWN_MS"), ("DEC_REARM_RUNS", "REARM_RUNS"),
                        ("DEC_MINE_RELEASE_MS", "MINE_RELEASE_MS")]:
    line = next(ln for ln in c_src.splitlines() if ln.startswith(f"#define {c_name} "))
    c_val = float(line.split()[2].rstrip("f"))
    assert abs(c_val - ns[py_name]) < 1e-6, f"{c_name}={c_val} but evaluate_stream.{py_name}={ns[py_name]}"


# the firmware's class order must be training/labels.py's (the model outputs in that order)
enum = re.search(r"enum \{ (CLS_\w+(?:, CLS_\w+)*), N_CLASSES \}", c_src).group(1).split(", ")
assert [e[4:].lower() for e in enum] == CLASSES, f"gesture_decoder.h {enum} != labels.py {CLASSES}"


def run_c(times, probs):
    d = Decoder()
    lib.decoder_init(ctypes.byref(d))
    events, holds, start = [], [], None
    for t, p in zip(times, probs):
        top = int(p.argmax())
        ev = lib.decoder_update(ctypes.byref(d), top, ctypes.c_float(p[top]), int(t))
        if ev in GEV:
            events.append((int(t), GEV[ev]))
            if ev == 5:
                start = int(t)
        elif ev == 6:
            holds.append((start, int(t)))
            start = None
    if start is not None:
        holds.append((start, int(times[-1])))
    return events, holds


def fake_stream(rng, n):
    """Model outputs that look like the real thing: runs of the same winner."""
    probs, cls = [], 0
    for _ in range(n):
        if rng.random() < 0.15:
            cls = rng.choice([0, 0, 0, 1, 2, 2, 3, 4, 5])
        p = np.full(len(CLASSES), 0.02, dtype=np.float32)
        p[cls] = rng.choice([0.3, 0.55, 0.59, 0.61, 0.8, 0.97])
        if rng.random() < 0.1:
            p[rng.randrange(len(CLASSES))] += 0.5
        probs.append(p / p.sum())
    times = 1000 + 50 * np.arange(n)
    return times, probs


rng = random.Random(1)
total = 0
for trial in range(300):
    times, probs = fake_stream(rng, 400)
    py = decode(times, probs, CLASSES)
    c = run_c(times, probs)
    assert py == c, f"trial {trial}: python {py} != C {c}"
    total += len(py[0])
print(f"decoder parity OK: 300 streams, {total} events, C and Python identical")
