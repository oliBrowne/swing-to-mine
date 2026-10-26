# Concept review council (Oct 26)

Four reviewers critiqued the concept (no code yet), each from a different angle:

- 🎮 **Minecraft player:** controls, Java vs Bedrock, crafting
- 🧠 **Embedded ML engineer:** gesture separability, streaming inference, hardware risks
- 🕹️ **UX / demo coach:** ergonomics, modes, live demo, shareability
- 🎓 **Instructor + recruiter:** ML depth, scope, checkpoint, resume

Items marked 🗳️ are **decisions for the team**. Everything else is a recommendation. Facts about Minecraft settings are partly from memory, so test them in-game.

---

## TL;DR: the 10 changes that matter most

1. **Fix the doc contradictions.** The README said walking uses WASD and aiming is a stretch goal; the requirements say no keyboard or mouse. (All 4 reviewers flagged this; the README is now updated to match "no keyboard, no mouse".)
2. **Target Minecraft Java Edition** on PC, with Raw Input on, Auto-Jump on and GUI Scale 4.
3. **Air mouse uses yaw + pitch only, never roll.** Roll is reserved for the swap twist.
4. **Keep steering while walking:** in move mode, turning the board left/right still turns the camera. Only tilt (pitch/roll) becomes WASD.
5. **Merge sword and mine for gameplay:** first stroke = click, another stroke within ~600 ms = hold left click, release after ~400 ms with no strokes. Keep "slash vs chop" as an ML experiment instead.
6. **Jump = a whole-body hop only** (no upward flick, which looks like a chop backswing), plus Auto-Jump so it's rarely needed.
7. **Add a cursor mode for inventory/crafting:** shake = `E` + cursor mode, punch = Shift+click, use the recipe book.
8. **Judge the live controller on false triggers per minute,** not clip accuracy. Most of that comes from post-processing (thresholds, cooldowns).
9. **De-risk the hardware first:** USB composite HID and a dummy ExecuTorch model on the real F407 in weeks 1–2.
10. **Build the handle ("sword") in week 1** and record all data with it, so the grip and sensor mount never change.

---

## 🎮 Minecraft: how it maps to the game

### Java vs Bedrock

- **Java Edition (recommended).** Keyboard and mouse is its native input, and our board *is* a keyboard + mouse. Java still has no built-in controller support, which makes a custom controller more interesting there.
- **Bedrock** on Windows also takes keyboard + mouse, so the board would work there too. Bedrock also supports gamepads natively; making the board a HID *gamepad* would give analog walking and controller-style menus. That's an idea for later, not a pivot.
- Consoles are out of scope.
- 🗳️ Confirm the demo laptop has Java Edition (it's paid).

### Every action, covered or not

| Action | Covered? | Plan |
|---|---|---|
| Attack / mine | ✅ | One "hold left click while striking" behavior |
| Place / use | ✅ | Punch = right click |
| Jump | ⚠️ | Hop gesture + Auto-Jump |
| Hotbar | ✅ | Wrist twist = scroll |
| Walk / strafe | ✅ | Move mode tilt, with yaw steering |
| Sprint | ➖ | Big forward tilt = Ctrl (Toggle Sprint on), nice-to-have |
| Sneak | ➖ | Not needed for the demo |
| Inventory (`E`) | ❌ → ✅ | **Shake** becomes a must-have gesture |
| Crafting / crafting table | ❌ → ✅ | Cursor mode + recipe book |
| Eating | ➖ | Play on Peaceful, so hunger doesn't matter |
| Furnace, chests, drop, chat, swimming | ➖ | Out of scope for the demo |
| Death / respawn | ➖ | `/gamerule doImmediateRespawn true` |
| Pause / menus | ➖ | Load the world before the demo; set up with a real keyboard |

### Inventory and crafting (cursor mode)

When a screen opens, Java releases the mouse, so board motion moves the OS cursor at Windows speed instead.

- **Shake** sends `E` and switches to **cursor mode**. Shake again to close and leave it.
- **Double-press the button** enters cursor mode without a key (for a crafting table opened with a punch).
- In cursor mode: lower air-mouse gain, movement/jump/swap off, cursor frozen during clicks, **punch = Shift + left click**.
- Use the **recipe book** (one click fills the grid) and **GUI Scale 4** (big slots). Turn off Windows "Enhance pointer precision".
- 🗳️ Cursor mode as a third mode, or button patterns on top of the two existing modes?

### Demo to design around (about 3 minutes, Survival on Peaceful)

1. Walk to a tree, mine 3 logs.
2. Shake → recipe book: planks, crafting table, sticks.
3. Close, swap to the crafting table, punch to place it, punch to open it.
4. Cursor mode → recipe book: wooden pickaxe.
5. Swap to the pickaxe, mine exposed stone.
6. Finale (optional): switch to Easy and slash a zombie.

**Minimum inputs:** hold left click, right click, scroll, `E`, WASD with yaw steering, mouse movement (two gains), Shift+click. Space and Ctrl are nice-to-haves.

---

## 🧠 Embedded ML: will the model work live?

### Gesture pairs that will be confused, and fixes

| Pair | Problem | Fix |
|---|---|---|
| Sword vs mine | Same motion, repeated | Merge for gameplay (see TL;DR #5). Optional: sword = horizontal slash, mine = vertical chop |
| Swap left vs right | A twist always comes back, so a right twist contains a left one | Classify only the direction of the *first* fast roll |
| Jump flick vs chop backswing | Both are "arm goes up fast" | Hop only: low-g dip, landing spike, little rotation |
| Swap twist vs aiming | Both rotate the wrist | Air mouse ignores roll |
| Punch vs slash | Fairly easy | Punch is mostly straight-line acceleration, little rotation |

### Streaming: what actually makes it feel good

- At ~20 inferences a second, "≤1 false trigger a minute" means **under 0.1% of windows can misfire**. Clip accuracy doesn't measure that.
- Post-processing does most of the work:
  - Motion-energy gate (don't classify when the board is calm)
  - Confidence threshold around 0.8+
  - 2–3 agreeing frames in a row
  - Cooldown of 300–500 ms per gesture
- **Latency:** you can't recognize a slash before it's done. Measure from the gesture's peak or end, not its start. Use an onset trigger and fire early when confidence is high.
- **Aim freeze reacts too late** if it waits for the model. Hold mouse output back ~50–80 ms and drop it if a gesture starts. Keep the crosshair frozen for the whole time mining is active, or the block resets.

### Data collection changes

- Record **continuous prompted sessions** (gestures mixed into normal movement, with a cue or metronome) instead of only one-gesture clips, so training matches what the live model sees.
- Augment with time shifts, ±15° rotations (grip changes), amplitude/time warp and noise.
- **Hard negatives:** play live, log every false trigger and add it to "nothing". This helps R6 more than anything else.
- Evaluate leave-one-person-out across **all** people (mean ± spread), not just one held-out person.
- Record all data on the **same physical sensor** (clone MPU-6050 chips differ).
- Ask recruited friends if their motion data can go in a public repo.

### Hardware risks

- **ExecuTorch on the real F407** (Lab 3 ran in Renode). The runtime plus ops can take 100+ KB, and only 128 KB of the 192 KB RAM is normal RAM (64 KB is CCM, which DMA can't reach). Run a dummy model on the board in week 1–2. Fallback: TFLite Micro or a hand-written int8 CNN with CMSIS-NN.
- **USB keyboard + mouse + serial on one port:** ST's Cube USB library struggles with composite devices. Use **TinyUSB**, or pick the mode at boot (button held at power-up = serial data mode). Put keyboard and mouse in one HID interface with report IDs.
- **MPU-6050 settings:** use the data-ready interrupt or its FIFO for steady 100 Hz, enable the ~42 Hz low-pass filter, gyro ±2000 °/s, accel ±16 g, add I2C bus-lockup recovery.

---

## 🕹️ UX and demo

### Ergonomics

- A bare PCB with jumper wires and a USB cable won't survive swinging. Build a **"sword" handle** in week 1: ~30 cm PVC pipe or paint stick, board + GY-521 zip-tied or hot-glued in a fixed orientation, cable taped along the handle with a strain-relief loop, 3 m USB cable.
- A cardboard or foam pixel sword on top is cheap and very photogenic.
- Holding the arm up to aim tires it out within a couple of minutes. Add a **rest pose** (board face-down or flat for >1 s pauses everything) and include that pose in the "nothing" data.

### Modes

- A toggle you can't see causes mode errors (the player watches the screen, not the LEDs).
- 🗳️ **Hold-to-move** (walk only while the button is held, release = stop) vs **toggle**. Hold-to-move can't get stuck. If hold-to-move wins, recalibrate moves to a 3 s still hold or a press at boot.
- If you keep the toggle, add feedback the player can feel or hear, such as a cheap buzzer or vibration motor.
- Release every held key and click on mode change, and add a firmware "kill" that releases everything.

### Air mouse feel

- Re-zero the gyro drift automatically whenever the board sits still for >1 s.
- Smooth and cap camera speed. Jitter on a projector looks broken and can make viewers queasy.
- Starting point: about 0.15° of camera turn per mouse count at default Java sensitivity (from memory, measure it).

### Live demo checklist

- Your own laptop, world preloaded, settings set, tested cable plus a spare.
- Calibrate before going up (board flat and still).
- Backup: the demo video, ready on the next slide.
- Show the hands: second camera picture-in-picture, or an LED/OLED on the handle showing the detected gesture. 🗳️ A debug overlay fed over USB serial is OK *for the presentation only* if you say so out loud; play itself stays software-free.
- Best moment: hand it to a classmate and let them play.

### Making it shareable

- A 20–30 s vertical clip: real swing next to the in-game result, split screen, one caption per move.
- Hook in the first 2 s: *"No keyboard. No mouse. AI on a $20 chip."*
- Show a stranger playing it, plus one overlay with the numbers (model size, latency, accuracy on a new person).

---

## 🎓 Course depth, scope and resume

### Is there enough ML?

Weeks 1–2 are mostly plumbing (I2C, USB, modes). Add a few cheap measured comparisons to make the ML story strong:

- **Non-ML baseline:** threshold/peak-detector rules vs the CNN (accuracy and false triggers per minute). The single most persuasive "why ML?" slide, and a fallback demo if the model slips. About half a day.
- **Model size sweep:** 3–4 widths, accuracy vs flash/RAM vs inference time.
- **float32 vs int8, pruned vs unpruned:** accuracy drop, size and speed on the M4 (DWT cycle counter).
- **Sensor ablation:** accel-only vs accel + gyro.
- **"Nothing" data vs false triggers:** how misfires drop as "nothing" data grows.
- Optional: knowledge distillation (big teacher → tiny student), since the course covered it.

### Scope

- 🗳️ **Minimum viable demo if time gets tight:** gestures on the chip + air mouse, and fall back to WASD on a real keyboard for walking. Week 4 is overloaded; move the streaming/post-processing loop into week 3 with a quick model.
- Define how each metric is measured:
  - **Latency:** toggle a GPIO at sample, inference and USB report and read it on a scope/logic analyzer, or film at 240 fps. Report window fill + inference + smoothing + USB poll separately.
  - **False triggers:** 10 minutes of scripted non-gesture play × 2 people.
- Write down window length, stride and model architecture.
- Name the toolchain and versions for R7 (CubeIDE, ExecuTorch commit, Python env).

### What the Nov 12/14 checkpoint should show

- Block diagram with a timing budget (100 Hz, window length, 50 ms stride, inference time, 1 ms USB poll)
- Model architecture with flash/RAM estimates against 512 KB / 192 KB
- Dataset protocol and leave-one-person-out evaluation
- Experiments table: each metric and how it's measured
- Live: board as a USB device + streaming sensor plots, ideally a dummy model running on the board
- Risks with fallbacks, cut order, owners

### Resume and post (numbers are targets)

- **Resume bullet:** "Built a gesture-controlled Minecraft controller on an STM32F407 (Cortex-M4): trained a 1D-CNN on a self-collected 6-axis IMU dataset (5 people, ~1,400 gestures), pruned and int8-quantized with ExecuTorch to [~40 KB], [<5 ms] per inference, [~90%] accuracy on an unseen user and [<1] false trigger/min, [~X×] fewer than a threshold baseline."
- **Post hook:** "I mined diamonds in Minecraft by swinging a $20 microcontroller. A [40 KB] neural net on the chip turns the swing into a click in [under 100 ms]."
- Recruiter note: the numbers and the baseline comparison make it read as engineering, not a hobby.

---

## 🗳️ Decisions for the team

| # | Decision | Recommended |
|---|---|---|
| 1 | Move mode: hold-to-move or toggle? | Hold-to-move |
| 2 | Cursor mode: third mode or button pattern? | Shake to enter/exit, plus double-press |
| 3 | Merge sword + mine for gameplay? | Yes, keep slash vs chop as an ML experiment |
| 4 | Fallback if behind: WASD on a real keyboard for walking? | Yes, as the cut-order fallback only |
| 5 | Debug overlay over USB serial during the presentation? | Yes, presentation only |
| 6 | Buzzer/vibration motor for mode feedback? | Optional, only if toggle is kept |
| 7 | Java Edition on the demo laptop? | Confirm |
| 8 | Accel source: MPU-6050 only (not the onboard LIS3DSH)? | Yes |
