# Project context (team notes)

Shared notes for both teammates and any AI assistant working in this repo: what's built, what's decided, what's still open, and who is doing what. **Last updated: Nov 23, 2026 (late evening; move-mode docs Nov 23).** If anything here disagrees with the code or with newer docs on `main`, the code and newer docs win; fix this file in the same change.

These notes contain no git or commit rules. Each person uses their own git workflow.

## 1. The project

Swing to Mine is a CU Boulder Embedded AI final project (Fall 2026) by Oliver Browne and Eli Garcia. An STM32F4 Discovery board with an MPU-6050 gyro/accelerometer is held like a sword. A small int8 CNN running **on the chip** recognizes gestures, and the board shows up on the PC as a USB keyboard + mouse that plays **Minecraft Java Edition**. No real keyboard or mouse is used.

**Goal:** simple, fun and working. **Team deadline: done by Thanksgiving (Thu Nov 26).** The final presentation with a live demo is on Dec 8. Keep the scope small; if we fall behind, cut in the order given in [roadmap.md](roadmap.md#if-we-fall-behind). The final code also has to go into the instructor's GitHub Classroom repo at the end. AI-written code is allowed for this project.

## 2. Who's doing what (Nov 23)

**Eli** (has the board):
- Flash and test the controller firmware in `firmware/swing_to_mine` (on `main` since Nov 23, PR #32; the model build and bug fixes are in the PR after it). Follow the "First test on the board" steps in its README, in order: USB shows up as a keyboard + mouse, then the sensor, then directions, then Minecraft, then the model.
- Report back what happened: whether USB is detected, which directions come out reversed (`YAW_SIGN`, `PITCH_SIGN`, `SIDE_SIGN`), how the air mouse and walking feel, and any restarts or dropouts.
- Record more data, especially **look-around idle** (waving the board around to look, with no gestures) from several people.
- **Free to write and change the controls code** (and propose control changes) based on what works in testing.

**Oliver** (away from the board until about Thanksgiving):
- Review Eli's test results and the numbers.
- Settle the open control questions (section 4) together with Eli; the test results should decide them.
- Write-up, final slides and demo plan.

Before starting something new, check this section and the open PRs so we don't both work on the same thing.

## 3. Status

**Working and tested on hardware:**
- **Data capture:** `firmware/capture` (MPU-6050 → USART2 PA2/PA3 at 115200, ~100 Hz, one line per sample: `ms,ax,ay,az,gx,gy,gz` raw ints) + `host/record.py`, through a CP2102 USB-serial adapter (driver notes in [setup-drivers.md](setup-drivers.md)). The ST-LINK virtual COM port stays silent; use the adapter's port.
- **Training pipeline** in `training/`: `labels.py`, `dataset.py`, `model.py`, `train.py`, `evaluate_stream.py` (replays whole recordings the way the board would), `export.py` (int8 ExecuTorch `.pte`).
- **Model on the board:** `firmware/model_test` runs the int8 model in **3.4 ms** per window at 168 MHz (≈117 KB flash, 41 KB RAM). int8 accuracy is about the same as float.
- CI builds the firmware and checks the Python scripts.

**Written but not tested on a board:**
- **`firmware/swing_to_mine`, the controller (PR #32, merged Nov 23).** It includes the USB keyboard + mouse, gyro calibration, air mouse, the button/mode/LED logic from [controls.md](controls.md), move-mode tilt walking + sprint + jump flick, the rest-pose pause, and the gesture decoder. It has only been checked on a PC (83 logic tests, decoder identical to `evaluate_stream.py`) and in the Renode simulator. Since Nov 23 it also builds **with the int8 model linked in** (123 KB flash, 45 KB RAM), and in the simulator a real recording played through a fake sensor makes the model on the simulated chip fire gestures that reach the USB code as the right clicks and scroll steps (`sim/renode_gestures.sh`). USB on a real PC, the real sensor, the axis directions and all the tune values are **unchecked**. All move-mode rules are in `src/tilt_walk.c` + `src/config.h`, and `MOVE_JUMP_ENABLED 0` turns the jump off.
- `firmware/live_test` (sensor → model → LEDs) was never run with the sensor attached; PR #32 mostly replaces it.

**Known hardware issue:** the board restarted once on a hard swing (probably a loose wire or a power dip). Check the wiring before demos.

## 4. Controls

Full spec: [controls.md](controls.md). ✅ = decided, ❓ = open (test results should decide it).

- ✅ Hold the board **upright** like a sword handle, both when playing and in recordings.
- ✅ **Three modes** with LEDs: **look** (green), **move** (orange), **inventory** (blue). A red blink means a gesture fired.
- ✅ **Button:** single press switches look ⇄ move. Holding ~1 s opens and closes the inventory (`E`). A double press toggles crouch (taps `Shift`; set Minecraft's Sneak to **Toggle**). Holding 3 s resyncs the board's mode with the game.
- ✅ **Look mode:** works like an air mouse (turning speed → mouse, with a dead zone). Fast motions freeze the camera and count as gestures. Gestures only work in look mode.
- ✅ **Move mode:** the neutral angle is zeroed on entry and gestures are off. Tilt forward ~15° = walk (`W`); a steep lean (~40°) held 0.3 s = sprint until you come back near level. Turning steers.
- ❓ **Move-mode extras** (in the firmware, not yet tested on hardware; Eli to tune on the board): tip back ~15° = `S`, side tilt ~22° = strafe `A`/`D`, and jump as an upward flick **only in move mode**. [controls.md](controls.md#move-mode-walking-and-sprinting) now describes exactly what the code does. PR #11 (the older proposal) was closed as superseded.
- ✅ **Inventory:** turning the board moves the cursor (slower), and a press = left click. There is no right-click or shift-click, and gestures are paused.
- ✅ **Hotbar:** a flick left/right sends one mouse-wheel step.
- ❓ Freeze speed for the air mouse: 250 °/s by default; the replay suggests ~180 °/s may feel better ([results.md](results.md)).
- ❓ Auto gyro recalibration in the rest pose (suggested, not agreed).
- ❓ All angle and speed numbers marked *tune*.

**Open questions from the code review of PR #32** (to settle by testing on the board):
- ❓ **Sensor reconnect:** after a wire blip the firmware recalibrates the gyro (camera frozen meanwhile). Should it keep the old calibration instead?
- ❓ **Set-down pause:** it can also trigger when the board is held steady like a TV remote for 1 s. Keep the rule or change it? (Button presses while paused are now ignored, as controls.md says.)
- ❓ **Move-mode jump flick** is ON (`MOVE_JUMP_ENABLED 1`). On the recordings it catches ~91% of jumps, with ~1 false jump per minute while looking around. Keep it on, tune it, or turn it off?
- ❓ **Smaller items:**
  - Sprint sends `Ctrl`+`W`, which closes a browser tab when testing outside Minecraft. Test in Minecraft or Notepad.
  - The camera axes are skewed for ~0.5 s after hard swings.

Fixed on Nov 23 (no longer open): power-on calibration while held in a hand (PR #34), buttons sending keys while paused, the USB stall when no sensor answers, and held keys not being re-sent after replugging USB.

## 5. Gestures, data and the model

- **Classes** (source of truth: `training/labels.py`): `nothing`, `sword` (side swipe → one left click), `mine` (keep chopping → hold left click), `place` (straight jab forward → right click), `swap_left` / `swap_right` (hotbar flicks → scroll). `jump` is recorded but not trained.
- **Recording** (`host/record.py`): cues come in random order, in rounds of 20, with Enter between rounds. Unlabeled time counts as `nothing`. Cues that were clearly done wrong go into `BAD_CUES` in `training/dataset.py`; they are skipped, not deleted. Data rules: [../data/README.md](../data/README.md).
- **People:** P01, P02, P03, P05, P06, P07, P10 and P11 (IDs only, never names). P05, P06 and P07 were added by Eli on Nov 22; some of their bad cues are not marked yet.
- **Trigger rules** (decoder in `training/evaluate_stream.py`, copied into the firmware): the model runs every 50 ms on a 1 s window. A gesture fires when confidence is ≥ 0.60 on 3 runs in a row. After that there is a 300 ms cooldown, and the same gesture can't fire again until it has stopped winning for 3 runs. Mine lets go after 400 ms without winning.
- **Results** ([results.md](results.md)): leave-one-person-out accuracy was ~96–99% of windows with 4 people; with all 8 people (Nov 23) it is 93.6–99.7% per unseen person (sword is the weakest class, 91% on average). In the replay, ~93–97% of cues fire the right gesture, with ~0.04–0.1 false triggers per minute between cues.
- **Weak spots:** (1) looking around causes ~0.2–1.4 false gestures per minute, mostly `place`, so more look-around idle data is the most useful next recording; (2) slow swings (one person's `sword` is ~75% right).
- **Tried without gain:** mirroring, speed augmentation, mixup, ensembles and bigger models. More people's data is what helps.

## 6. Repo conventions

- Keep the docs in sync with the code (see [../AGENTS.md](../AGENTS.md)). Numbers go in `results.md` with how they were measured.
- No personal data in the repo: people are P-numbers only, with no emails, home paths or device serials.

## 7. Next steps

1. Eli: first board test of `firmware/swing_to_mine` (no model first, then `SWING_MODEL=ON`; how to set up ExecuTorch from scratch is in its README), and report the results.
2. Fix the direction signs and tune values from that test; decide the ❓ items in section 4.
3. Record look-around idle from several people and retrain.
4. Play-test in Minecraft, measure lag and false triggers, and record the demo video (before Nov 26).
