# Roadmap

**Goal: working controller done by Thanksgiving (Thu Nov 26).** That leaves Dec 8 for the presentation only, with nothing left to build.

Rough budget: about 30 hours each (the course minimum), or about 6 hours a week each for 5 weeks.

Owners are suggestions: **Oliver** on firmware and hardware, **Eli** on the recording script and training, since he wrote the Lab 2 training script. Swap freely. **Everyone records data**, including both of us and any friends we recruit, because more people means a model that works for anyone.

```
Week          Oct 26   Nov 2    Nov 9    Nov 16   Nov 23   ...   Dec 8
Hardware      ██████
USB + modes            ██████
Data + model                    ██████
On the board                             ██████
Polish                                            ████
Present                                                         ★
```

## Week 1 · Oct 26 – Nov 1 · Talk to the board

- [ ] Order the gyro (GY-521 MPU-6050) · *Oliver*
- [ ] STM32CubeMX project for the F407 Discovery: clock, I2C1, USB OTG FS, button, LEDs · *Oliver*
- [ ] Read the MPU-6050 at 100 Hz (`WHO_AM_I`, wake it, burst read of accel + gyro) · *Oliver*
- [ ] Stream the 6 channels to the PC over USB serial · *Oliver*
- [ ] Host script that records labeled clips to CSV (`host/record.py`) · *Eli*
- [x] Decide how moving works: toggle + tilt to walk (see [gesture-notes.md](gesture-notes.md#move-mode-tilt-to-walk-default)) · *both*

**Done when:** you can wave the board and watch 6 live lines on the PC, then save a labeled clip.

## Week 2 · Nov 2 – Nov 8 · It's a controller (no ML yet)

- [ ] USB composite HID: the board shows up as a keyboard + mouse · *Oliver*
- [ ] Button: short press switches look/move, long press recalibrates; LEDs show the mode · *Oliver*
- [ ] Look mode: gyro "air mouse" with drift calibration · *Oliver*
- [ ] Move mode: tilt to walk (or whatever we pick in week 1) · *Oliver*
- [ ] Record pilot data: about 20 reps of each gesture · *everyone*
- [ ] **Nov 6:** idea and team locked in (course milestone, nothing to submit)

**Done when:** you can walk around and look around in Minecraft with only the board.

## Week 3 · Nov 9 – Nov 15 · First model + checkpoint

- [ ] Train a first small CNN on the pilot data and check which gestures get confused · *Eli*
- [ ] Full data collection: Oliver, Eli and 2–3 recruited friends, about 25 min each (plan in [gesture-notes.md](gesture-notes.md#data-collection-plan)) · *everyone*
- [ ] Checkpoint slides: live look/move demo, data plots, first accuracy numbers · *both*
- [ ] **Nov 12 / 14:** checkpoint presentation

**Done when:** the checkpoint is presented and the full dataset is recorded.

## Week 4 · Nov 16 – Nov 22 · ML on the chip

- [ ] Train on the full dataset, testing on a person left out of training · *Eli*
- [ ] Prune + int8-quantize, export with ExecuTorch (Lab 3 pipeline) · *Eli*
- [ ] Run the model on the board every ~50 ms on a sliding window · *Oliver*
- [ ] Turn predictions into inputs: smoothing, cooldowns, hold-to-mine, aim freeze during swings · *Oliver*
- [ ] Measure: accuracy, lag (motion → input), flash/RAM use, false triggers per minute · *both*

**Done when:** you can mine, fight, place, jump and swap items in Minecraft with gestures alone.

## Week 5 · Nov 23 – Nov 26 · Polish and ship 🦃

- [ ] Play-test, then tune the thresholds that annoy us
- [ ] Record the demo video
- [ ] README: exact setup and run steps (written for a random person on the internet)
- [ ] Results section with the numbers and a confusion matrix

**Done when:** a stranger could clone the repo and get it working, and the video exists.

## After Thanksgiving

- Final slides, short Canvas report, and the **Dec 8** presentation (1:30–4 pm)
- Post about it (LinkedIn / GitHub), using the demo video
- Stretch goals only if we want to: walk-in-place model, extra gestures, a second board

## If we fall behind

Cut in this order: extra gestures → air-mouse polish → tilt-to-walk (use a simpler fallback) → number of people in the dataset. **Never cut:** the on-chip model and the live demo.
