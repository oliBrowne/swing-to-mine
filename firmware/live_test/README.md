# firmware/live_test

> **Status (Nov 13): built and flashed, but NOT tested with a sensor.** The board it was flashed on has no MPU-6050 wired (it reads WHO_AM_I = `0xFF` and waits). Nothing below the "waiting for sensor" step has run yet: live inference, the decoder, the LEDs for gestures and the false-trigger count are all untested.

Live sensor into the model, result on the LEDs. It reads the MPU-6050 at 100 Hz, keeps the last 1 s (100 samples × 6 channels), runs the int8 gesture model every 50 ms, and lights an LED when a gesture is detected. No UART or USB: you read what it's doing over the ST-LINK with `scripts/watch.py`.

## LEDs

| LED | Meaning |
|---|---|
| **red, steady** | waiting for the sensor (no answer on I2C; it retries every second) |
| green flash | sword |
| orange, on | mine (stays on while mining keeps winning) |
| red flash | place |
| blue flash | swap left or right |
| all off | nothing |
| all four blinking fast | ExecuTorch error (`watch.py` shows the stage and code) |

**Decoder:** a gesture fires when the model's softmax probability for it is ≥ 0.60 on 3 runs in a row (150 ms). After it fires, new gestures are ignored for 300 ms, and the same gesture cannot fire again until the model has named something else 3 runs in a row (otherwise one swing fires twice, because it stays inside the 1 s window). Mine turns on the same way and lets go once mine has not been the top pick for 400 ms. The settings are at the top of `src/main.cpp` and must match `training/evaluate_stream.py`, which replays recordings through the same rules (Nov 14: the C++ and Python decoders gave identical events on 11 replayed sessions; this version has not been built or flashed yet).

## What it needs

- The MPU-6050 (GY-521) wired as in [docs/wiring.md](../../docs/wiring.md): VCC → 5V (or 3V), GND → GND, SCL → **PB6**, SDA → **PB9**, AD0 open or GND. The UART adapter isn't needed.
- The same setup as [`../model_test`](../model_test/README.md): Linux or WSL, ExecuTorch 1.3.0 in `~/executorch`, `arm-none-eabi-gcc`, CMake, Ninja, OpenOCD.

## Build and flash

From the repo root, in the ExecuTorch env (`source ~/executorch/et-env/bin/activate`):

```bash
# 1. a trained model, exported int8 (writes training/out/model_test/model.pte)
python training/train.py
python training/export.py

# 2. ExecuTorch + CMSIS-NN ops, once (~3 min) -> ~/executorch/executorch-build/cortex-m4-cmsis
bash firmware/model_test/scripts/build_executorch.sh

# 3. build
cd firmware/live_test
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../model_test/arm-gcc-toolchain.cmake
cmake --build build                # build/live_test.elf + .bin, prints the size (~105 KB flash)

# 4. flash (from WSL: first `usbipd attach --wsl --busid <BUSID>` in Windows, `usbipd list` shows it)
bash ../model_test/scripts/flash.sh build/live_test.bin
```

## Check it

```bash
python scripts/watch.py            # one snapshot
```

First check `imu_ok=1` and a `who_am_i` value (0x68 on a genuine chip; clones may answer something else, which is fine), and that `samples` and `inferences` go up between two snapshots. With the board lying still you should see roughly `[0, 0, 2048, 0, 0, 0]` in `last sample` (1 g on one axis, gyro near 0; the axis depends on how it lies).

**False triggers, board still** (the first number to record):

```bash
python scripts/watch.py --seconds 120     # leave the board on the desk, don't touch it
```

It prints the samples per second (should be ~100), inferences per second (~20), and how many detections happened in those 2 minutes, per gesture. Do the same while holding it and looking around normally (like look mode) for a more realistic number. Put the results in [docs/results.md](../../docs/results.md).

**Then try the gestures** (Oliver): do each one ~10 times and note how often the right LED lights, then run `watch.py` to read the per-gesture counts. Also check the worst-case timing line: `sample->decision` should stay well under 10 ms (one sample period). The model alone is ~3.4 ms.

If nothing lights for real gestures, `watch.py` shows `model says:` with the last class and probability. If the right class wins but below 0.60, lower `kMinProb` in `src/main.cpp`.

## Files

| File | What it is |
|---|---|
| `src/main.cpp` | ring buffer, scaling (same numbers as `training/dataset.py`), model every 50 ms, decoder, LEDs, `g_*` status variables |
| `src/imu.c/.h` | MPU-6050 over I2C1: `../capture`'s driver, changed for the 42 MHz bus clock used here |
| `CMakeLists.txt` | reuses `../model_test`'s startup, board, C library hooks, `memcpy` and the patched pool op |
| `scripts/watch.py` | reads `g_*` from the running board over the ST-LINK; `--seconds N` counts detections |
