# firmware/

Firmware for the STM32F4 Discovery (STM32F407VG), built with CMake + `arm-none-eabi-gcc` (see the toolchain table in architecture.md). See [../docs/architecture.md](../docs/architecture.md) for how the pieces connect.

**Status:** [`capture/`](capture/) is a small stand-alone firmware for recording data (MPU-6050 → UART text stream). It builds but hasn't been tested on hardware yet. [`model_test/`](model_test/) runs the trained gesture model with ExecuTorch on the board and measures it: **3.43 ms** per inference (int8, CMSIS-NN), tested on the board Nov 13. [`live_test/`](live_test/) feeds the MPU-6050 into the model every 50 ms and shows gestures on the LEDs: built and flashed Nov 13, **not yet tested with a sensor**. The main firmware below doesn't exist yet.

## Planned layout

```
firmware/
  capture/                  ✅ exists: stand-alone data-capture firmware (MPU-6050 -> UART)
  model_test/               ✅ exists: model speed test (ExecuTorch int8 on the board, results over the ST-LINK)
  live_test/                ⚠️ exists, untested with a sensor: MPU-6050 -> model every 50 ms -> LEDs
  swing_to_mine/            main firmware (CMake, ST HAL/CMSIS as submodules)
    Core/Src/main.c         init + 100 Hz loop
    App/
      imu_mpu6050.c/.h      sensor driver + gyro bias calibration
      ring_buffer.c/.h      last 1 s of samples (100 x 6 int16)
      model_runner.c/.h     wraps the inference runtime: window -> class scores
      gesture_decoder.c/.h  scores -> game events (thresholds, cooldowns, strike/hold)
      air_mouse.c/.h        yaw + pitch -> mouse dx/dy, delay buffer, auto re-zero
      tilt_walk.c/.h        pitch/roll -> WASD with a deadzone
      mode_manager.c/.h     CALIBRATE / LOOK / MOVE / INVENTORY, button, LEDs, rest pose
      hid_out.c/.h          builds keyboard + mouse reports, release_all()
      usb_stream.c/.h       USB serial: sample stream + '#' debug lines
      timing.c/.h           DWT cycle counter, GPIO debug toggles
    Model/                  exported model + scaling constants (generated, don't edit)
```

## Module sketches

Directional only; adjust names freely, but keep the boundaries.

| Module | Main functions | Notes |
|---|---|---|
| `imu_mpu6050` | `imu_init()`, `imu_read(sample_t*)`, `imu_calibrate_bias()` | ±16 g, ±2000 °/s, DLPF ~42 Hz, 100 Hz, data-ready interrupt; don't fail on clone `WHO_AM_I` values |
| `ring_buffer` | `rb_push(sample)`, `rb_window(int8_t out[6][100])` | scaling constants come from the model export |
| `model_runner` | `model_init()`, `model_classify(window, scores[8])` | ExecuTorch first; TFLM or CMSIS-NN fallback |
| `gesture_decoder` | `decoder_update(scores, imu_energy) -> event` | energy gate, ~0.8 threshold, 2–3 agreeing frames, per-class cooldown, strike → click / hold |
| `air_mouse` | `air_mouse_update(gyro, mode) -> dx, dy` | yaw + pitch only, never roll; ~60 ms delay buffer that a gesture onset can cancel |
| `tilt_walk` | `tilt_walk_update(accel) -> keys` | MOVE mode only |
| `mode_manager` | `mode_on_button(event)`, `mode_on_gesture(event)`, `mode_tick()` | follows [docs/controls.md](../docs/controls.md): single press LOOK/MOVE, hold 1 s INVENTORY (`E`), double press crouch (taps `Shift`); releases held inputs on change |
| `hid_out` | `hid_send(keys, buttons, dx, dy, wheel)`, `hid_release_all()` | one HID interface, report IDs for keyboard and mouse |
| `usb_stream` | `stream_sample(sample)`, `stream_debug(fmt, ...)` | format in architecture.md "USB serial stream" |

## Milestones (match the roadmap)

- [ ] **M1 · Board talks:** project builds and flashes; LED blinks; button read
- [ ] **M2 · Sensor:** MPU-6050 read at 100 Hz, streamed over USB serial in the agreed format (UART version written in `capture/`, untested on hardware)
- [ ] **M3 · USB HID:** board shows up as keyboard + mouse (TinyUSB composite with serial)
- [x] **M4 · Dummy model:** any tiny exported model runs on the board; record flash/RAM/time (Nov 13: the real trained model, int8, 3.43 ms, 117 KB flash, 41 KB RAM; [`model_test/`](model_test/), [results](../docs/results.md))
- [ ] **M5 · No-ML controller:** modes, air mouse, tilt walk working in Minecraft
- [ ] **M6 · Real model:** trained model + decoder drive gestures in-game
- [ ] **M7 · Measured:** latency and memory numbers in `docs/results.md`

M3 and M4 are the biggest unknowns: do them early.

## Wiring

See [../docs/hardware.md](../docs/hardware.md).
