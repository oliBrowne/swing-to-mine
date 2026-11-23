# Project context (handoff for teammates and their AI assistants)

This file sums up what Oliver and his AI assistant decided and learned in chat, so that anyone (Eli, or Eli's own AI agent) who clones the repo starts with the same picture. **Last updated: Nov 22, 2026.** If anything here disagrees with the code or with newer docs on `main`, the code and the newer docs win; please fix this file in the same change.

## 1. The project in one paragraph

Swing to Mine is a CU Boulder Embedded AI final project (Fall 2026) by Oliver Browne (`oliBrowne`) and Eli Garcia. An STM32F4 Discovery board with an MPU-6050 gyro/accelerometer is held like a sword. A small int8 CNN running **on the chip** recognizes gestures, and the board shows up on the PC as a USB keyboard + mouse that plays **Minecraft Java Edition**. No real keyboard or mouse is used.

**Goal:** simple, fun, working, worth posting / putting on a résumé. **Deadline the team set: done by Thanksgiving, Thu Nov 26.** Dec 8 is the final presentation (live demo). Keep scope modest; when in doubt, cut (order in [roadmap.md](roadmap.md#if-we-fall-behind)). The final code must also be copied into the instructor's GitHub Classroom repo at the end.

**AI-written code is fine for this project** (Oliver confirmed; unlike some earlier labs).

## 2. Where things stand (as of Nov 22)

Done and tested on hardware:
- **Data capture:** `firmware/capture` (MPU-6050 → USART2 PA2/PA3 at 115200, ~100 Hz, one line per sample: `ms,ax,ay,az,gx,gy,gz` raw ints) + `host/record.py`. Works with a CP2102 USB-serial adapter (needs the Silicon Labs CP210x driver on Windows; see [setup-drivers.md](setup-drivers.md)). The ST-LINK virtual COM port is silent; use the adapter's port.
- **Training pipeline** in `training/`: `labels.py`, `dataset.py`, `model.py`, `train.py`, `evaluate_stream.py` (replays whole recordings like the board would), `export.py` (int8 ExecuTorch `.pte`).
- **Model on the board:** `firmware/model_test` runs the int8 model in **3.4 ms** per window at 168 MHz (≈117 KB flash, 41 KB RAM). int8 accuracy ≈ float.
- CI (GitHub Actions) builds the firmware and checks the Python scripts.

Built but **not yet tested**:
- `firmware/live_test`: sensor → model every 50 ms → LEDs. Flashed, never tested with the sensor attached.
- The newer trigger rules (see section 5) are in the PC replay and in `firmware/live_test/src/main.cpp`, but that firmware has **not been rebuilt and flashed** since.

Written Nov 22 but **checked only without hardware** (Oliver is away from the board until about Thanksgiving):
- **`firmware/swing_to_mine`**, the controller (PR #32): USB HID keyboard + mouse on CN5 (TinyUSB 0.21.0 as a git submodule, register-level like the other firmware, no ST HAL), power-on gyro calibration, air mouse (turning speed → mouse, dead zone, freeze above 250 °/s, 60 ms delay a gesture cancels), button/mode/LED state machine exactly as in [controls.md](controls.md), move-mode tilt → `W A S D` + sprint + jump flick, rest-pose pause, and the gesture decoder hooked to clicks / hold / scroll in look mode. Builds without the model by default (`SWING_MODEL=OFF`); `-DSWING_MODEL=ON` links the ExecuTorch model like `live_test` (that path compiles but was never linked or run).
- Checked: 82 PC logic tests, decoder identical to `evaluate_stream.py` on 300 random streams, Renode simulator boot + mode switching with a fake MPU-6050, recordings replayed through the air mouse ([results.md](results.md#controller-without-hardware-nov-22-on-the-pc)). CI runs all of these.
- **Not checked:** USB on a real PC, the real sensor with this firmware, axis signs (`YAW_SIGN`, `PITCH_SIGN`, `SIDE_SIGN` in `src/config.h` are guesses from the recordings), any *tune* number. First-test steps are in [firmware/swing_to_mine/README.md](../firmware/swing_to_mine/README.md#first-test-on-the-board-oliver-when-back-with-the-hardware).
- All move-mode rules live in `src/tilt_walk.c` + `src/config.h` because PR #11 is on hold; the move-mode jump flick can be switched off with `MOVE_JUMP_ENABLED 0`.

**Still to do:**
1. First hardware test of `firmware/swing_to_mine` (USB enumeration, sensor, axis signs), then with `SWING_MODEL=ON`.
2. Tune in Minecraft: speed, freeze speed (replay suggests trying ~180 °/s, see results.md), tilt angles.
3. Measure lag, false triggers, memory ([results.md](results.md)).
4. Hardware check: the board **restarted once on a hard swing** (likely a loose wire or power dip). Check the wiring before demos. The controller restarts its sensor and recalibrates by itself after ~0.2 s of failed reads.

Honest summary: the ML side is ahead of plan; the controller is now written but has never touched hardware, so the first board test is the critical path.

## 3. Open pull requests (Nov 22) and what to do with them

Merged Nov 22: #26 (P11 data, more P10 data, clean-up, look-around idle test), #29 (README status), #30 and #31 (this file). #27 was closed (its data is in #26). Oliver merges; each merge needs his explicit OK.

| PR | What | Note |
|---|---|---|
| #11 | Controls: move-mode poses, jump in move mode only | **On hold:** Oliver isn't sure about it yet. `main`'s controls.md still shows the older "hop in look mode"; `training/labels.py` already treats jump as a move-mode rule. Ask Oliver before relying on either. |
| #32 | `firmware/swing_to_mine`: the controller firmware (USB keyboard + mouse, modes, air mouse, walking, gestures), tests, Renode check, CI | Nov 22, not hardware-tested. Needs Oliver's OK to merge. |

Other branches: `host/look-idle-test` (record.py `--look-minutes` look-around idle test, Nov 13, not merged); `firmware/usb-hid-test` (Oct 29 TinyUSB test, not merged; superseded by #32); `dad-data`, `dad-data-2`, `mom-data`, `p10-data-retrain` (data branches, contents already on `main`).

## 4. Controls (decided by Oliver)

Full spec in [controls.md](controls.md). The tip-back / strafe poses and "jump only in move mode" come from PR #11, which Oliver has put **on hold** (not yet confirmed); the rest is on `main`. Summary:

- Board held **upright** like a sword handle, in play and in all recordings.
- **Three modes**, shown by LEDs: **look** (green), **move** (orange), **inventory** (blue). Red blink = a gesture fired. All-four-LED patterns give feedback while holding the button.
- **Blue button:**
  - single press = toggle look ⇄ move;
  - hold ~1 s = open **and** close inventory (sends `E`);
  - double press = crouch on/off (taps `Shift`; in Minecraft set **Sneak = Toggle**);
  - hold 3 s = resync when the board and the game disagree (e.g. after closing a chest by other means). Chest flow: place/use, then hold 3 s.
- **Look mode** (fighting, building): Wii-remote / air-mouse style. Gyro rotation **rate** → mouse movement, with a dead zone. Fast motions freeze the camera and are treated as gestures instead. Gestures active here only.
- **Move mode:** neutral angle is zeroed on entry (~0.2 s still); gestures are off. Tilt forward ~15° = walk (`W`); ~40° held 0.3 s = sprint, stays on until back near level (fallback: a forward flick); tip back = `S`; side tilt ~20–25° = strafe `A`/`D`; left/right turning steers the camera (no up/down look). **Jump = quick upward flick, move mode only** (from PR #11, on hold), done with a simple accelerometer threshold rule, not the model.
- **Inventory:** gyro moves the cursor (same pointing, slower); button press = left click (pick up / drop). No right-click or shift-click on purpose. Gestures paused. The board tracks inventory state itself, so it can desync if the inventory is closed some other way (that's what resync is for).
- **Hotbar:** quick flick left / right sends one mouse-wheel step (Java has no next/prev hotbar key).
- Changing mode releases any held inputs. Set down flat and still = pause output.
- 🟡 Suggested, not confirmed: auto gyro recalibration in the rest pose.

## 5. Gestures, data and the model

**Classes** (source of truth: `training/labels.py`): `nothing`, `sword` (side swipe → one left click), `mine` (keep chopping → hold left click), `place` (straight jab forward → right click), `swap_left`, `swap_right` (hotbar flicks → scroll). `jump` is recorded (optional) but not trained; old names `slash/chop/punch` map to the new ones; `hop` and `shake` were dropped.

**Recording** (`host/record.py`): random gesture order in rounds (default 20 cues), big on-screen cues, press Enter between rounds (`q` stops and saves). Recording keeps running through breaks; unlabeled time counts as `nothing`. The recorder stops by itself if the board goes quiet for 1 s. Oliver explicitly does **not** want the same gesture 10× in a row. Data layout and rules: [../data/README.md](../data/README.md).

**People in the data** (IDs only, no names in the repo): P01, P02, P03, P10, P11. Eli's brothers may record more. Recorded cues that were clearly done wrong go into `BAD_CUES` in `training/dataset.py` (skipped, never deleted).

**How a gesture is cut out of a cue:** around the strongest motion burst inside the cue (this lifted unseen-person accuracy from ~95% to ~97.7%).

**Trigger rules** (decoder in `training/evaluate_stream.py`): model every 50 ms on a 1 s window; confidence ≥ 0.60; 3 agreeing guesses in a row; 300 ms cooldown; a gesture can't re-fire until it has stopped winning for 3 runs; mine releases after 400 ms without winning. Keep `evaluate_stream.py` and `firmware/live_test` in step.

**Results so far** (details and how each was measured: [results.md](results.md)):
- Leave-one-person-out, per window: ~96–99% for most people; P10 ~96–97%.
- Replay like the board: ~93–97% of cues fire the right gesture; ~0.04–0.1 false triggers per minute between cues.
- **Weak spot 1: look-around idle.** Waving the board around to look (no gesture) causes ~0.2–1.4 false gestures per minute, mostly `place`, about 10× the between-cue rate. Only P10 has look-around idle data. **More look-around idle recordings (Oliver, Eli, others) are the next most useful data.**
- **Weak spot 2: slow swings.** P10 swings at about half the speed of others; his `sword` is only ~75% right.
- Tried and **did not help**: mirroring, speed augmentation (with 2 people), mixup, ensembles, a wider/deeper model. Max pooling over time did help a little and is the default. More people's data is what helps.

## 6. How we work in this repo

- Work on a branch and open a PR; **don't push to `main`.** Oliver asks explicitly before each merge (he often says "push and merge" together, but each merge still needs his ask).
- **Commits are authored as `oliBrowne`** and contain **no AI co-author / "Generated by" trailer lines.**
- Don't run two AI sessions on the same task at once (it has already caused duplicated PRs once).
- Keep the docs in sync with the code in the same change (see [../AGENTS.md](../AGENTS.md)). Numbers go in `results.md` with how they were measured.
- No personal data in the repo: people are P-numbers only; no emails, home paths or device serials.
- Plain, non-jargon explanations are preferred when talking to Oliver.

## 7. Suggested next steps (in order)

1. Review and merge the controller PR (#32), Oliver's call. Decide on PR #11 (controls), also Oliver's call.
2. On the board: `firmware/swing_to_mine` without the model first (USB, sensor, signs), then `SWING_MODEL=ON`. `firmware/live_test` is now mostly superseded by it.
3. Check the loose-wire restart.
4. Record look-around idle from several people; retrain.
5. Play-test in Minecraft, tune, measure, record the demo video (before Nov 26).
