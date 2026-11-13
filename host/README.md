# host/

PC-side Python scripts. **None of these run during play**: the controller needs no PC software. They're for recording data, debugging and the presentation.

**Status:** `record.py` exists (simple version: cues in the terminal, no plan file yet). Tested against a simulated board, not yet against real hardware.

## Planned scripts

| Script | What it does | Needed by |
|---|---|---|
| `record.py` | Reads the USB serial stream, shows on-screen cues ("next: hop"), and saves a continuous session plus its label file | Week 1–2 |
| `live_plot.py` | Live plot of the 6 channels, for debugging and the checkpoint demo | Week 1 |
| `label_tool.py` | Optional: review a session and fix label times | if needed |
| `overlay.py` | Presentation only: shows the detected gesture from `#` debug lines on screen | Week 5, optional |

## `record.py`

Board setup first: flash [`firmware/capture`](../firmware/capture/) and wire it as in [docs/wiring.md](../docs/wiring.md). Run these inside the Python venv (one-time setup: [firmware/capture/README.md](../firmware/capture/README.md#python-virtual-environment)).

```
pip install -r host/requirements.txt

# is the board talking? (keep it still: expect ~100 Hz, ~1.00 g, gyro near 0)
python host/record.py --port COM5 --check

# 10 of every gesture in random order, in rounds of 20 cues, one cue every ~3 s
python host/record.py --port COM5 --person P01 --session s01 --reps 10

# only some gestures
python host/record.py --port COM5 --person P01 --session s02 --gestures hop punch --reps 20

# "nothing" data: move around normally for 5 minutes, no cues
python host/record.py --port COM5 --person P01 --session s03 --nothing-minutes 5
```

- Gestures come in random order, 20 cues per round (`--round-size`). Between rounds it stops and waits for you to press Enter (or `y`), so you can take a break; type `q` to finish early. Recording keeps running during the break and that time counts as "nothing". Each cue is a big box like `GO:   H O P     3 / 20` (3rd cue of the round) with a one-line how-to and a beep. Do the gesture once, then move normally until the next cue.
- A mistake can't be detected: if you do the wrong gesture, note the cue number it printed so it can be removed later.
- It writes `data/raw/P01/s01.csv` and `data/raw/P01/s01.labels.csv` in the format from [../docs/architecture.md](../docs/architecture.md#recorded-data). Each cue becomes a label covering the 1.5 s after it.
- Cue time is a rough label; training snaps it to the motion peak.
- Gesture names come from [`training/labels.py`](../training/labels.py). Person ids must look like `P01`.
- Ctrl+C stops early and keeps what was recorded. It won't overwrite an existing session unless you add `--overwrite`.
- Planned later: a `--plan` file for longer mixed sessions.

## Setup

Python 3 in a virtual environment, then `pip install -r host/requirements.txt` (just `pyserial` for now; `matplotlib` comes with `live_plot.py`). Steps for Linux/WSL and Windows: [firmware/capture/README.md](../firmware/capture/README.md#python-virtual-environment). Newer Ubuntu/WSL refuses `pip install` outside a venv.
