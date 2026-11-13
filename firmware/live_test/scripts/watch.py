"""Read the live test's state from the running board through the ST-LINK.

    python firmware/live_test/scripts/watch.py              # one snapshot
    python firmware/live_test/scripts/watch.py --seconds 120  # count detections over 2 minutes

Reads the g_* variables (OpenOCD mdw; the core keeps running).
"""
import argparse
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

CLASSES = ["nothing", "sword", "mine", "place", "swap_left", "swap_right"]
HERE = Path(__file__).resolve().parent


def read(elf):
    nm = subprocess.run(["arm-none-eabi-nm", "-S", str(elf)], capture_output=True, text=True, check=True).stdout
    syms = {p[3]: (int(p[0], 16), int(p[1], 16)) for p in (l.split() for l in nm.splitlines())
            if len(p) == 4 and p[3].startswith("g_")}
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
    if not words.get("g_stage"):
        sys.exit("Could not read the board:\n" + (out.stdout + out.stderr)[-1500:])
    return words


def i32(w):
    return struct.unpack("<i", struct.pack("<I", w))[0]


def f32(w):
    return struct.unpack("<f", struct.pack("<I", w))[0]


def show(w):
    u = lambda n: w[n][0]
    print(f"stage={u('g_stage')} error={u('g_error')} imu_ok={u('g_imu_ok')} "
          f"who_am_i=0x{u('g_who_am_i') & 0xFF:02X} imu_errors={u('g_imu_errors')} t={u('g_now_ms') / 1000:.1f} s")
    print(f"samples={u('g_samples')} inferences={u('g_inferences')} "
          f"last sample={[i32(x) for x in w['g_last_sample']]}")
    if u("g_inferences"):
        print(f"model says: {CLASSES[i32(u('g_last_class'))]} (p={f32(u('g_last_prob')):.2f}) "
              f"scores={[round(f32(x), 2) for x in w['g_last_scores']]}")
    print(f"detections={u('g_events')} " + " ".join(f"{c}={n}" for c, n in zip(CLASSES[1:], w['g_event_count'][1:])))
    print(f"worst: model {u('g_infer_cycles_max') / 168e3:.2f} ms, sample->decision {u('g_loop_cycles_max') / 168e3:.2f} ms, "
          f"gap between samples {u('g_sample_gap_ms_max')} ms")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default=str(HERE.parent / "build" / "live_test.elf"))
    ap.add_argument("--seconds", type=float, default=0, help="watch this long and report detections")
    args = ap.parse_args()
    first = read(args.elf)
    show(first)
    if args.seconds <= 0:
        return
    end = time.time() + args.seconds
    while time.time() < end:
        time.sleep(min(10, max(0, end - time.time())))
    last = read(args.elf)
    print("\n--- after", args.seconds, "s ---")
    show(last)
    d = lambda n: last[n][0] - first[n][0]
    dt = (last["g_now_ms"][0] - first["g_now_ms"][0]) / 1000
    print(f"\nover {dt:.1f} s on the board: {d('g_samples')} samples ({d('g_samples') / dt:.1f} Hz), "
          f"{d('g_inferences')} inferences ({d('g_inferences') / dt:.1f}/s), {d('g_events')} detections")
    for i, c in enumerate(CLASSES[1:], 1):
        n = last["g_event_count"][i] - first["g_event_count"][i]
        if n:
            print(f"  {c}: {n}")


if __name__ == "__main__":
    main()
