# firmware/

Firmware for the STM32F4 Discovery (STM32F407VG), built with CMake + `arm-none-eabi-gcc` (see the toolchain table in architecture.md). See [../docs/architecture.md](../docs/architecture.md) for how the pieces connect.

**Status:** [`capture/`](capture/) is a small stand-alone firmware for recording data (MPU-6050 → UART text stream). It works on the board and recorded all our data. [`model_test/`](model_test/) runs the trained gesture model with ExecuTorch on the board and measures it: **3.43 ms** per inference (int8, CMSIS-NN), tested on the board Nov 13. [`live_test/`](live_test/) feeds the MPU-6050 into the model every 50 ms and shows gestures on the LEDs: built and flashed Nov 13, **not yet tested with a sensor**. [`swing_to_mine/`](swing_to_mine/) is the controller itself (USB keyboard + mouse, air mouse, look/move/inventory modes, button, LEDs, gestures): **written Nov 22 and checked only without hardware** (PC tests, Renode simulator); never run on a board yet.

## Layout

```
firmware/
  capture/          ✅ works on hardware: stand-alone data-capture firmware (MPU-6050 -> UART)
  model_test/       ✅ works on hardware: model speed test (ExecuTorch int8, 3.4 ms)
  live_test/        ⚠️ built, untested with a sensor: MPU-6050 -> model every 50 ms -> LEDs
  swing_to_mine/    ⚠️ built + simulator-checked, untested on hardware: the controller
    src/config.h        every number to tune (axes, speeds, angles, button timing)
    src/main.c          1 kHz loop: button, LEDs, USB; sensor at 100 Hz; model every 50 ms
    src/controller.c    modes, button actions, gestures -> keys/clicks, LEDs
    src/button.c        single / double / hold 1 s / hold 3 s
    src/motion.c        gyro calibration, gravity direction
    src/air_mouse.c     turning speed -> mouse, dead zone, freeze on fast motion
    src/tilt_walk.c     move mode: W/A/S/D, sprint, jump flick
    src/gesture_decoder.c  same trigger rules as training/evaluate_stream.py
    src/hid_out.c ...   TinyUSB keyboard + mouse
    test/  sim/         PC tests, recording replay, Renode check
  lib/              git submodules: TinyUSB 0.21.0, ST CMSIS headers (git submodule update --init)
```

Details, build steps and a first hardware test plan: [swing_to_mine/README.md](swing_to_mine/README.md).

## Milestones (match the roadmap)

- [ ] **M1 · Board talks:** project builds and flashes; LED blinks; button read
- [ ] **M2 · Sensor:** MPU-6050 read at 100 Hz, streamed over USB serial in the agreed format (UART version written in `capture/`, untested on hardware)
- [ ] **M3 · USB HID:** board shows up as keyboard + mouse (TinyUSB composite with serial) · Nov 22: keyboard + mouse written in `swing_to_mine/` (no serial), not yet tried on a PC
- [x] **M4 · Dummy model:** any tiny exported model runs on the board; record flash/RAM/time (Nov 13: the real trained model, int8, 3.43 ms, 117 KB flash, 41 KB RAM; [`model_test/`](model_test/), [results](../docs/results.md))
- [ ] **M5 · No-ML controller:** modes, air mouse, tilt walk working in Minecraft · Nov 22: written + simulator-checked, not on hardware
- [ ] **M6 · Real model:** trained model + decoder drive gestures in-game · Nov 22: hooked up (`SWING_MODEL=ON`), not built with ExecuTorch yet
- [ ] **M7 · Measured:** latency and memory numbers in `docs/results.md`

M3 and M4 are the biggest unknowns: do them early.

## Wiring

See [../docs/hardware.md](../docs/hardware.md).
