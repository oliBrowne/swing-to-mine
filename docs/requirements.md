# Project requirements (loose)

## Why we're doing this

A **simple, fun, interesting** project that shows real ML running on hardware. It should be easy to post about and good on a resume, and it should **not take over our lives**. If a choice makes the project bigger without making it more fun or more impressive, skip it.

## What we're building

A handheld STM32F4 Discovery board (plus an MPU-6050 gyro) that works as a full Minecraft controller over USB. A small neural network on the chip recognizes gestures, and the gyro handles looking around. No keyboard, no mouse, and no software on the PC.

## Must have

- **R1 · Plug and play:** the board shows up as a USB keyboard + mouse. Nothing to install on the PC.
- **R2 · Gestures on the chip:** sword swing, mine, place block, jump, swap item left/right and "nothing", classified by an int8 model running on the F407.
- **R3 · Look and move:** the button switches look mode (air mouse) and move mode; a long press recalibrates; the LEDs show the mode.
- **R4 · Our own dataset:** recorded from at least 4 people, with a large "nothing" class.
- **R5 · Feels responsive:** gesture to in-game input in about 100 ms or less.
- **R6 · Doesn't misfire:** no more than about 1 accidental gesture a minute during normal play.
- **R7 · Reproducible:** the README lets someone else build, flash and play.
- **R8 · Course deliverables:** checkpoint (Nov 12/14), final presentation (Dec 8), short report, repo with a good README.

## Nice to have

- Walk-in-place detection instead of tilt-to-walk
- Extra gestures (shake for inventory, crouch, sprint)
- Comparing model sizes: accuracy vs. lag vs. memory
- A 3D-printed or taped-up "sword" handle for the board

## Out of scope

- A custom PCB, wireless/Bluetooth, or a battery
- Any game other than Minecraft (Java Edition)
- Perfect accuracy. "Fun to play" is the bar.
- Anything that needs software running on the PC during play

## How we'll know it worked

| Measure | Target |
|---|---|
| Gesture accuracy on a person not in the training data | 90% or better |
| Lag from motion to input | about 100 ms or less |
| Accidental gestures | about 1 per minute or fewer |
| Model size | fits comfortably in the F407VG's 1 MB flash / 192 KB RAM |
| Done date | before Thanksgiving (Nov 26) |
| The real test | someone in class plays it and grins |

## Constraints

- **Hardware:** STM32F407 (Cortex-M4, 1 MB flash, 192 KB RAM), MPU-6050 on I2C, one user button, 4 LEDs.
- **Toolchain:** STM32CubeIDE / CubeMX for firmware; PyTorch + ExecuTorch for the model (same as Lab 3).
- **Time:** about 30 hours each.
- **Team:** Oliver Browne, Eli Garcia.

See also: [roadmap.md](roadmap.md) · [gesture-notes.md](gesture-notes.md) · [hardware.md](hardware.md)
