"""Record gesture data from the Swing to Mine board.

Reads the board's serial stream (one line per sample, 100 Hz) and saves a
session the way docs/architecture.md describes:

    data/raw/<person>/<session>.csv          t_ms,ax,ay,az,gx,gy,gz
    data/raw/<person>/<session>.labels.csv   start_ms,end_ms,label

While recording, the terminal shows a big box with the gesture to do ("GO: S W O R D  3 / 20").
Gestures come in random order, 20 cues per round. Between rounds it waits for you to
press Enter, so you can take a break.
Each cue becomes one row in the labels file, covering the 1.5 s after the cue.
Anything without a label counts as "nothing", so move around normally between
cues.

Examples (use your own COM port; on a Mac it looks like /dev/tty.usbserial-XXXX):

    # 1. check the board is talking (prints a few samples and the sample rate)
    python host/record.py --port COM5 --check

    # 2. record 10 of each gesture in random order, in rounds of 20 cues, one cue every ~3 s
    python host/record.py --port COM5 --person P01 --session s01 --reps 10

    # 3. only some gestures
    python host/record.py --port COM5 --person P01 --session s02 --gestures sword mine --reps 20

    # 4. only jumps
    python host/record.py --port COM5 --person P01 --session s04 --gestures jump --reps 20

    # 5. "nothing" data: just move around normally for 5 minutes, no cues
    python host/record.py --port COM5 --person P01 --session s03 --nothing-minutes 5

    # 6. idle test: look around like in look mode (not dead still) for 3 minutes,
    #    with a new look-around prompt every few seconds; no gestures
    python host/record.py --port COM5 --person P01 --session s20 --look-minutes 3

Stop early with Ctrl+C: everything recorded so far is kept.
"""

import argparse
import math
import random
import sys
import threading
import time
from pathlib import Path

try:
    import serial  # pip install pyserial
except ImportError:
    sys.exit("pyserial is missing. Install it with:  pip install pyserial")

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "training"))
from labels import CLASSES, OPTIONAL_GESTURES  # noqa: E402  (the one shared class list)

COLUMNS = "t_ms,ax,ay,az,gx,gy,gz"
ACCEL_COUNTS_PER_G = 2048.0      # MPU-6050 at +-16 g
GYRO_COUNTS_PER_DPS = 16.4       # MPU-6050 at +-2000 deg/s
LABEL_LENGTH_MS = 1500           # how long after a cue the gesture is assumed to happen
# Mine is continuous chopping (it holds left click while you keep chopping), so
# its label is longer and the next cue waits a bit more.
LABEL_LENGTH_MS_FOR = {"mine": 2500}

# Shown on screen with each cue so it's clear what motion to do.
HOW_TO = {
    "sword": "one SIDE SWIPE, like swinging a sword",
    "mine": "keep CHOPPING (pickaxe swings) for ~2 s",
    "place": "one straight JAB forward, NO tilt",
    "swap_left": "one quick hotbar flick to the LEFT",
    "swap_right": "one quick hotbar flick to the RIGHT",
    "jump": "one quick UPWARD flick, board held upright",
    "nothing": "IDLE: hold still or move naturally, NO gesture",
}


# Name shown on screen when it differs from the label saved in the file.
SHOWN_AS = {"nothing": "idle"}

# --look-minutes: idle test where you keep looking around like in look mode
# (air mouse) instead of holding still. A new prompt every few seconds keeps
# the motion varied. None of these are gestures: the whole stretch is "nothing".
LOOK_PROMPTS = [
    "slowly look LEFT, then back",
    "slowly look RIGHT, then back",
    "look UP at the sky, then level",
    "look DOWN at your feet, then level",
    "scan the horizon left to right",
    "aim at a block and hold it, tiny corrections",
    "follow a mob walking past",
    "quick glance over your shoulder",
    "look around freely, like exploring",
    "turn all the way around",
    "small aiming wobbles, like lining up a block",
    "rest your arm, board still in hand",
]
LOOK_PROMPT_SECONDS = (4, 8)     # a new prompt every 4-8 s


def banner(title, line2="", fill="#"):
    """Print a big box that is hard to miss, e.g.  M I N E   3 / 20."""
    width = 60
    print()
    print(fill * width)
    print(fill + " " * (width - 2) + fill)
    for text in (title, line2):
        if text:
            print(fill + text.center(width - 2) + fill)
    print(fill + " " * (width - 2) + fill)
    print(fill * width)


def spaced(name):
    """mine -> 'M I N E', swap_left -> 'S W A P   L E F T' (easier to read from far away)."""
    return "   ".join(" ".join(word.upper()) for word in name.split("_"))


class BoardReader:
    """Reads lines from the board in the background and keeps the newest sample.

    Every good sample is passed to on_sample(t_ms, values). Lines starting with
    '#' are info or error messages from the board and get printed.
    """

    def __init__(self, port, baud, on_sample):
        self.ser = serial.Serial(port, baud, timeout=0.2)
        self.on_sample = on_sample
        self.last_t_ms = None
        self.last_rx = None        # computer time of the newest sample
        self.samples = 0
        self.gaps = 0
        self.info_line = None
        self.running = True
        self.thread = threading.Thread(target=self._loop, daemon=True)

    def start(self):
        self.thread.start()

    def _loop(self):
        while self.running:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="ignore").strip()
            if not line:
                continue
            if line.startswith("#"):
                if line.startswith("# swing-to-mine"):
                    self.info_line = line
                else:
                    print(f"  [board] {line}")
                continue
            parts = line.split(",")
            if len(parts) != 7:
                continue                      # column names or a half line: skip
            try:
                t_ms, *values = (int(p) for p in parts)
            except ValueError:
                continue
            if self.last_t_ms is not None:
                step = t_ms - self.last_t_ms
                if step < 0:
                    print("  [warning] board time went backwards: did the board reset?")
                elif step > 15:
                    self.gaps += 1            # a sample was lost (expected step is 10 ms)
            self.last_t_ms = t_ms
            self.last_rx = time.time()
            self.samples += 1
            self.on_sample(t_ms, values)

    def wait_for_data(self, timeout_s=5.0):
        end = time.time() + timeout_s
        while time.time() < end:
            if self.samples > 0:
                return True
            time.sleep(0.05)
        return False

    def stalled(self, seconds=1.0):
        """True if the board hasn't sent a sample for `seconds` (unplugged, reset or crashed)."""
        return self.last_rx is None or time.time() - self.last_rx > seconds

    def close(self):
        self.running = False
        self.thread.join(timeout=1)
        self.ser.close()


def no_data_help(port):
    sys.exit(
        f"No samples from {port} after 5 s. Things to check:\n"
        "  - the right COM port (Windows: Device Manager > Ports)\n"
        "  - adapter TX goes to board PA3 and adapter RX to board PA2 (they cross)\n"
        "  - adapter GND is connected to board GND\n"
        "  - the board is flashed with firmware/capture and powered\n"
        "  - press the black reset button on the board\n"
        "Wiring: docs/wiring.md"
    )


def check(args):
    """Print samples for a few seconds and report the rate and an at-rest sanity check."""
    rows = []

    def on_sample(t_ms, values):
        rows.append((t_ms, values))
        if len(rows) % 20 == 1:
            print(f"  {t_ms},{','.join(str(v) for v in values)}")

    board = BoardReader(args.port, args.baud, on_sample)
    board.start()
    print(f"Listening on {args.port} at {args.baud} baud. Keep the board still...")
    if not board.wait_for_data():
        board.close()
        no_data_help(args.port)
    time.sleep(args.check_seconds)
    board.close()

    if len(rows) < 2:
        sys.exit("Got almost no samples.")
    span_s = (rows[-1][0] - rows[0][0]) / 1000.0
    rate = (len(rows) - 1) / span_s if span_s > 0 else 0
    ax, ay, az, gx, gy, gz = (sum(v[i] for _, v in rows) / len(rows) for i in range(6))
    g = math.sqrt(ax * ax + ay * ay + az * az) / ACCEL_COUNTS_PER_G
    print()
    print(f"Samples:   {len(rows)} in {span_s:.1f} s = {rate:.1f} Hz (should be about 100)")
    print(f"Lost:      {board.gaps} gaps")
    print(f"Gravity:   {g:.2f} g (should be about 1.00 when the board is still)")
    print(f"Gyro avg:  {gx / GYRO_COUNTS_PER_DPS:.1f}, {gy / GYRO_COUNTS_PER_DPS:.1f}, "
          f"{gz / GYRO_COUNTS_PER_DPS:.1f} deg/s (should be near 0 when still)")
    if board.info_line:
        print(f"Board:     {board.info_line}")


def record(args):
    # Default: every model gesture, plus an IDLE cue (labelled "nothing") and jump.
    gestures = args.gestures or (CLASSES + OPTIONAL_GESTURES)
    allowed = CLASSES + OPTIONAL_GESTURES
    unknown = [g for g in gestures if g not in allowed]
    if unknown:
        sys.exit(f"Unknown gesture(s) {unknown}. Choose from: {', '.join(allowed)}")
    if not (args.person.startswith("P") and args.person[1:].isdigit()):
        sys.exit("Use an anonymous person id like P01, not a name.")

    out_dir = Path(args.out) / args.person
    out_dir.mkdir(parents=True, exist_ok=True)
    data_path = out_dir / f"{args.session}.csv"
    labels_path = out_dir / f"{args.session}.labels.csv"
    if data_path.exists() and not args.overwrite:
        sys.exit(f"{data_path} already exists. Pick a new --session or add --overwrite.")

    data_file = open(data_path, "w", newline="")
    labels_file = open(labels_path, "w", newline="")
    labels_file.write("start_ms,end_ms,label\n")
    wrote_header = [False]

    def on_sample(t_ms, values):
        if not wrote_header[0]:
            info = board.info_line or "# swing-to-mine v1 rate_hz=100 accel_fs_g=16 gyro_fs_dps=2000"
            data_file.write(f"{info} person={args.person} session={args.session}\n")
            data_file.write(COLUMNS + "\n")
            wrote_header[0] = True
        data_file.write(f"{t_ms},{','.join(str(v) for v in values)}\n")

    board = BoardReader(args.port, args.baud, on_sample)
    board.start()
    print(f"Listening on {args.port}...")
    if not board.wait_for_data():
        board.close()
        # Don't leave empty session files behind.
        data_file.close()
        labels_file.close()
        data_path.unlink(missing_ok=True)
        labels_path.unlink(missing_ok=True)
        no_data_help(args.port)

    # Every gesture `reps` times, all mixed in random order, then split into
    # rounds of 20 cues with a break between rounds.
    cues = [g for g in gestures for _ in range(args.reps)]
    random.shuffle(cues)
    size = max(1, args.round_size)
    rounds = [cues[i:i + size] for i in range(0, len(cues), size)]
    total = len(cues)

    try:
        print("Get ready. Hold the board with your normal grip.")
        for n in (3, 2, 1):
            print(f"  {n}...")
            time.sleep(1)

        if args.look_minutes:
            banner("IDLE TEST:  LOOK AROUND",
                   f"{args.look_minutes:g} min, like in look mode. NO gestures", fill="=")
            end = time.time() + args.look_minutes * 60
            last_prompt = None
            while time.time() < end and not board.stalled():
                prompt = random.choice([p for p in LOOK_PROMPTS if p != last_prompt])
                last_prompt = prompt
                left = int(end - time.time())
                print(f"  [{left // 60}:{left % 60:02d} left]  {prompt}")
                time.sleep(min(random.uniform(*LOOK_PROMPT_SECONDS), max(0.0, end - time.time())))
            if board.stalled():
                banner("!!  BOARD STOPPED SENDING DATA  !!",
                       "check the cable, then start a NEW session", fill="!")
            else:
                banner("DONE", "You can put the board down", fill="=")
        elif args.nothing_minutes:
            print(f"Recording 'nothing' for {args.nothing_minutes} min: aim, walk, rest, "
                  "fidget. Don't do any gestures.")
            end = time.time() + args.nothing_minutes * 60
            while time.time() < end:
                left = int(end - time.time())
                print(f"\r  {left // 60}:{left % 60:02d} left   ", end="", flush=True)
                time.sleep(1)
            print()
        else:
            done = 0
            for round_number, cues in enumerate(rounds, 1):
                # Between rounds, wait until the person says they're ready.
                # Recording keeps running during the break; nothing is labeled
                # then, so it counts as "nothing".
                if round_number > 1:
                    banner(f"BREAK:  {done} of {total} cues done", "rest as long as you like", fill="=")
                    try:
                        answer = input(f"  Press Enter (or y) to start round {round_number} of {len(rounds)}, "
                                       "or type q to stop here: ")
                    except EOFError:      # no keyboard attached: stop cleanly
                        answer = "q"
                    if answer.strip().lower() in ("q", "n"):
                        print("Stopping here. Keeping what was recorded.")
                        break
                banner(f"ROUND {round_number} of {len(rounds)}:  {len(cues)} random gestures",
                       "do each one once, right after its GO box", fill="=")
                for n in range(args.round_break, 0, -1):
                    print(f"  starting in {n}...")
                    time.sleep(1)
                for i, gesture in enumerate(cues, 1):
                    if board.stalled():
                        break
                    done += 1
                    start = board.last_t_ms
                    banner(f"GO:   {spaced(SHOWN_AS.get(gesture, gesture))}     {i} / {len(cues)}", HOW_TO.get(gesture, ""))
                    print(f"  (cue {done} of {total})\a")
                    length = LABEL_LENGTH_MS_FOR.get(gesture, LABEL_LENGTH_MS)
                    labels_file.write(f"{start},{start + length},{gesture}\n")
                    labels_file.flush()
                    # Random gap so the gaps between gestures aren't all identical.
                    time.sleep(max(1.6, args.gap + random.uniform(-0.5, 1.0))
                               + (length - LABEL_LENGTH_MS) / 1000)
                if board.stalled():
                    # The board stopped sending (cable bumped, reset button, crash).
                    # Cues after this point would have no data, so stop here.
                    banner("!!  BOARD STOPPED SENDING DATA  !!",
                           "check the cable, then start a NEW session", fill="!")
                    print(f"  Stopped after cue {done} of {total}. Everything before that is saved.")
                    break
            time.sleep(1.0)  # let the last gesture finish
            banner("DONE", f"{done} of {total} cues recorded. You can put the board down", fill="=")
    except KeyboardInterrupt:
        print("\nStopped early. Keeping what was recorded.")
    finally:
        board.close()
        data_file.close()
        labels_file.close()

    print(f"\nSaved {board.samples} samples ({board.samples / 100:.0f} s) to {data_path}")
    print(f"Saved labels to {labels_path}")
    if board.gaps:
        print(f"Note: {board.gaps} gaps in the data (lost samples).")


def main():
    p = argparse.ArgumentParser(description="Record gesture sessions from the Swing to Mine board.")
    p.add_argument("--port", required=True, help="serial port, e.g. COM5 or /dev/tty.usbserial-1410")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--check", action="store_true", help="just test the connection and print stats")
    p.add_argument("--check-seconds", type=float, default=5.0)
    p.add_argument("--person", help="anonymous id like P01")
    p.add_argument("--session", help="session name like s01")
    p.add_argument("--gestures", nargs="+", help="which gestures to cue (default: all of them, including idle and jump)")
    p.add_argument("--reps", type=int, default=10, help="cues per gesture (default 10)")
    p.add_argument("--round-size", type=int, default=20, help="cues per round before a break (default 20)")
    p.add_argument("--round-break", type=int, default=5, help="seconds of countdown once a round starts (default 5)")
    p.add_argument("--gap", type=float, default=3.0, help="seconds between cues (default 3)")
    p.add_argument("--nothing-minutes", type=float, help="record only 'nothing' for this many minutes")
    p.add_argument("--look-minutes", type=float,
                   help="idle test: look around like in look mode for this many minutes, "
                        "with a new prompt every few seconds; no gestures, all 'nothing'")
    p.add_argument("--out", default=str(REPO / "data" / "raw"), help="output folder (default data/raw)")
    p.add_argument("--overwrite", action="store_true", help="replace an existing session")
    args = p.parse_args()

    if args.check:
        check(args)
    elif not (args.person and args.session):
        p.error("--person and --session are needed to record (or use --check)")
    else:
        record(args)


if __name__ == "__main__":
    main()
