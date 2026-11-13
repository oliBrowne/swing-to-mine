"""Read the model test's results from the running board through the ST-LINK.

    python firmware/model_test/scripts/readback.py [build/model_test.elf] [training/out/model_test]

Finds the g_* variables in the .elf, reads them with OpenOCD (mdw, the core
keeps running) and prints the time per inference and the scores next to the
ones PyTorch/ExecuTorch-on-the-PC gave for the same windows.
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ELF = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "build" / "model_test.elf"
MODEL = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE.parents[2] / "training" / "out" / "model_test"
CORE_HZ = 168e6

nm = subprocess.run(["arm-none-eabi-nm", "-S", str(ELF)], capture_output=True, text=True, check=True).stdout
syms = {p[3]: (int(p[0], 16), int(p[1], 16)) for p in (l.split() for l in nm.splitlines())
        if len(p) == 4 and p[3].startswith("g_") and p[3] != "g_log"}
args = ["openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
        "-c", "transport select hla_swd", "-c", "init"]
for name, (addr, size) in sorted(syms.items()):
    args += ["-c", f"echo {{@@ {name}}}", "-c", f"mdw 0x{addr:08x} {max(1, size // 4)}"]
args += ["-c", "shutdown"]
out = subprocess.run(args, capture_output=True, text=True)

words, cur = {}, None
for line in (out.stdout + out.stderr).splitlines():
    m = re.match(r"@@ (\S+)", line.strip())
    if m:
        cur = m.group(1)
        words[cur] = []
        continue
    m = re.match(r"0x[0-9a-f]+:\s+(.*)", line.strip())
    if m and cur:
        words[cur] += [int(w, 16) for w in m.group(1).split()]
if "g_magic" not in words or not words["g_magic"]:
    sys.exit("Could not read the board:\n" + (out.stdout + out.stderr)[-1500:])

u = lambda n: words[n][0]
floats = lambda ws: [struct.unpack("<f", struct.pack("<I", w))[0] for w in ws]
print(f"running={u('g_magic') == 0x600D5EED} stage={u('g_stage')} error={u('g_error')} "
      f"inferences={u('g_iterations')}")
if u("g_stage") != 3:
    sys.exit("Not running: stage 1 = Program::load, 2 = load_method, 4 = execute, 5 = set_input failed "
             "(error 0x14 OperatorMissing, 0x21 MemoryAllocationFailed, 0x12 InvalidArgument)")
lo, hi = u("g_cycles_min"), u("g_cycles_max")
print(f"time per inference: {lo / CORE_HZ * 1e3:.3f} to {hi / CORE_HZ * 1e3:.3f} ms "
      f"({lo} to {hi} cycles at 168 MHz)")
print(f"model {u('g_pte_bytes')} bytes, method memory used {u('g_method_pool_used')} bytes")

scores = np.array(floats(words["g_scores"]))
ref = np.load(MODEL / "ref_scores.npy")
scores = scores.reshape(ref.shape)
float_ref = np.load(MODEL / "float_scores.npy")
for i, (s, r) in enumerate(zip(scores, ref)):
    print(f"  window {i}: board", " ".join(f"{v:7.3f}" for v in s), "| expected", " ".join(f"{v:7.3f}" for v in r))
print(f"max |board - expected|: {np.abs(scores - ref).max():.3g}   "
      f"max |board - float model|: {np.abs(scores - float_ref).max():.3g}   "
      f"same top class as float model: {(scores.argmax(1) == float_ref.argmax(1)).sum()} of {len(ref)}")
