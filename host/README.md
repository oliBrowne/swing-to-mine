# host/

PC-side Python scripts. **None of these run during play**: the controller needs no PC software. They're for recording data, debugging and the presentation.

**Status:** nothing here yet.

## Planned scripts

| Script | What it does | Needed by |
|---|---|---|
| `record.py` | Reads the USB serial stream, shows on-screen cues ("next: hop"), and saves a continuous session plus its label file | Week 1–2 |
| `live_plot.py` | Live plot of the 6 channels, for debugging and the checkpoint demo | Week 1 |
| `label_tool.py` | Optional: review a session and fix label times | if needed |
| `overlay.py` | Presentation only: shows the detected gesture from `#` debug lines on screen | Week 5, optional |

## `record.py` sketch

```
python host/record.py --port COM5 --person P01 --session s01 --plan plans/full.yaml
```

- The plan lists cues and counts (for example 40 x `hop`, mixed in random order, with gaps of normal movement).
- It writes `data/raw/P01/s01.csv` and `data/raw/P01/s01.labels.csv` in the format from [../docs/architecture.md](../docs/architecture.md#recorded-data).
- Cue time is a rough label; training snaps it to the motion peak.

## Setup

Python 3 + `pyserial`, `matplotlib`, `pyyaml`. Pin versions in `requirements.txt` when the first script lands.
