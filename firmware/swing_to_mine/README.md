# firmware/swing_to_mine: the controller

> **Status (Nov 22): written and checked without hardware. Never run on a real board yet.** It builds, the logic passes 82 tests on a PC, the gesture decoder fires exactly the same events as `training/evaluate_stream.py`, and it boots in the Renode simulator with a fake sensor and switches modes on simulated button presses. What has **not** been checked: USB on a real PC (no simulator models the USB port), the real MPU-6050 at the same time as USB, the model build with ExecuTorch linked in (the model code compiles against ExecuTorch 1.3.0 headers but was not linked), and every *tune* number in [`src/config.h`](src/config.h).

The board shows up on the PC as a **USB keyboard + mouse** (plug the micro-USB port **CN5** into the PC; keep the mini-USB ST-LINK cable plugged in too, as far as I know CN5 doesn't power the board) and plays Minecraft Java Edition the way [docs/controls.md](../../docs/controls.md) describes:

| | What it does |
|---|---|
| **Power on** | all four LEDs spin while the gyro calibrates, then green = look mode. On a table it takes ~1.5 s. In a hand, hold it reasonably steady: after ~3 s it takes the calmest second so far (a little less exact; the dead zone hides the rest). If it is being waved about it keeps spinning until you hold it steady for a second |
| **Look mode** (green) | turning the board moves the camera like a Wii remote; dead zone for hand shake; fast motion freezes the camera. Gestures: sword = left click, mine = hold left click (camera held still), place = right click, swap left/right = one scroll step |
| **Move mode** (orange) | single press from look mode. Hold still 0.2 s (orange blinks once), then tilt forward = `W`, back = `S`, sideways = `A`/`D`, steep forward for 0.3 s = sprint (`Ctrl`) until you come back near level; turning steers; quick upward flick = jump (`Space`). No gestures |
| **Inventory** (blue) | hold the button 1 s (sends `E`); the board points the cursor (slower), a press = left click; hold 1 s again closes it (sends `E`) and goes back to look or move |
| **Double press** | crouch: taps `Shift` (set Minecraft's Sneak to **Toggle**); mode LED blinks twice |
| **Hold 3 s** | resync: switch between play and inventory without sending a key (e.g. after opening a chest with a right click); LED blinks 3 times |
| **Set it down flat** | still for 1 s: all output pauses, the mode LED breathes; pick it up to carry on |
| **Red LED** | short blink = a gesture fired. Fast blinking with nothing else = the sensor isn't answering (check the wires) |

Without the model (`SWING_MODEL=OFF`, the default and what CI builds) everything works except gestures.

## Build

Needs `arm-none-eabi-gcc`, CMake and Ninja (same as the other firmware), plus the git submodules (TinyUSB 0.21.0 and ST's CMSIS headers):

```bash
git submodule update --init          # once, from the repo root
cd firmware/swing_to_mine
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../model_test/arm-gcc-toolchain.cmake
cmake --build build                  # build/swing_to_mine.elf / .bin / .hex, ~21 KB flash, ~4 KB RAM
```

**With the gesture model:** first do the ExecuTorch and export steps from [`../live_test`](../live_test/README.md#build-and-flash) (train + `training/export.py` → `training/out/model_test/model.pte`, and `build_executorch.sh` once), then:

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../model_test/arm-gcc-toolchain.cmake -DSWING_MODEL=ON
cmake --build build
```

Flash `build/swing_to_mine.bin` like the other firmware (`bash ../model_test/scripts/flash.sh build/swing_to_mine.bin`, or drag-and-drop the `.bin` onto the ST-LINK drive).

## First test on the board

Go in this order; each step only needs the ones before it.

1. **No model, no sensor:** flash, plug CN5 into the PC. Red should blink fast (no sensor). The PC should find a keyboard + mouse called *Swing to Mine controller* (Windows: Device Manager → Human Interface Devices). If it doesn't show up at all, that's the USB part.
2. **Sensor wired:** LEDs spin, then green. Open Notepad, then Paint: turn the board slowly and the mouse pointer should move; flick it fast and the pointer should stop. Press the button once: orange; tilt forward and Notepad should fill with `w`. Hold 1 s: blue and an `e` appears. Double press in Notepad: nothing visible (Shift alone types nothing), that's fine.
3. **Directions:** if left/right, up/down, forward/back or A/D come out reversed, flip `YAW_SIGN`, `PITCH_SIGN` or `SIDE_SIGN` in [`src/config.h`](src/config.h). I guessed them from the recordings and couldn't check them without the board.
4. **Minecraft:** set Sneak = Toggle, Auto-Jump = On. Then tune speed (`LOOK_COUNTS_PER_DEG`), the freeze speed (`FREEZE_DPS`) and the tilt angles. Note what you changed in `docs/results.md`.
5. **With the model** (`SWING_MODEL=ON`): gestures in look mode, red blink on each.

While it runs, these variables can be read over the ST-LINK (e.g. adapt `../live_test/scripts/watch.py`): `g_imu_ok`, `g_who_am_i`, `g_samples`, `g_mode` (0 calibrate, 1 look, 2 move, 3 inventory), `g_paused`, `g_usb_mounted`, `g_fwd_deg` / `g_side_deg` (move-mode tilt), `g_inferences`, `g_last_class`, `g_last_prob`, `g_events[]` (decoder events), `g_model_error`, `g_loop_us_max`.

## Checks without hardware

```bash
make -C test               # logic tests on the PC: 82 checks (button timing, modes, air mouse, walking, gestures, rest pose)
make -C test parity        # decoder == decode() in training/evaluate_stream.py (needs numpy)
make -C test replay && test/replay ../../data/raw/P10/s42.csv   # a recording through the air mouse
bash sim/renode_check.sh /path/to/renode    # boots in the Renode simulator with a fake sensor, presses the button
```

CI runs all four on every push. The replay numbers are in [docs/results.md](../../docs/results.md#controller-without-hardware-nov-22-on-the-pc).

## Files

| File | What it does |
|---|---|
| `src/config.h` | **every number to tune**: axes and their signs, speeds, dead zone, freeze speed, tilt angles, button timing, jump on/off |
| `src/main.c` | the loop: 1 kHz button/LEDs/USB, sensor samples, model every 50 ms; `g_*` debug variables |
| `src/controller.c` | modes, button actions, taps, rest pose, gestures → keys/clicks, LEDs (plain logic, tested on the PC) |
| `src/button.c` | debounce, single / double / hold 1 s / hold 3 s, instant click in the inventory |
| `src/motion.c` | gyro calibration at power-on, gravity direction ("up") from accel + gyro |
| `src/air_mouse.c` | turning speed → mouse, dead zone, freeze on fast motion, 60 ms delay a gesture can cancel |
| `src/tilt_walk.c` | move mode: neutral angle, W/A/S/D, sprint, jump flick (everything move-mode, kept in one place because PR #11 is on hold) |
| `src/gesture_decoder.c` | model winners → events, same rules as `training/evaluate_stream.py` and `../live_test` |
| `src/model_executorch.cpp` / `model_none.c` | the int8 model via ExecuTorch (`SWING_MODEL=ON`) or nothing |
| `src/hid_out.c`, `usb_descriptors.c`, `tusb_config.h` | USB: one HID interface, report 1 = keyboard, 2 = mouse, polled every 1 ms |
| `src/startup.c` | startup + full interrupt table (USB needs IRQ 67) |
| `test/` | PC tests, decoder parity, recording replay |
| `sim/` | Renode simulator check with a fake MPU-6050 |

Shared with the other firmware: `../model_test/src/board.c` (168 MHz clock, LEDs, 1 ms tick), `../model_test/stm32f407vg.ld`, `../live_test/src/imu.c` (MPU-6050 over I2C1).

## Differences from the plan in docs/architecture.md

- **No USB serial (CDC) yet**, only keyboard + mouse. Data recording still goes through `../capture` and the UART adapter. Debug info is in the `g_*` variables instead.
- Files are flat in `src/` instead of `Core/` + `App/`, and there is no ST HAL (registers directly, like the other firmware here).
- **Jump** is the move-mode upward flick from PR #11 / project-context.md, since the model has no jump class. It can be turned off with `MOVE_JUMP_ENABLED 0` in `config.h` while #11 is on hold.
- Rest pose: "flat" means less than half of gravity along the handle; it doesn't recalibrate the gyro (the 🟡 auto-recalibration idea isn't built).

## Checklist

- [x] Builds (no model) in CI
- [x] Logic tests, decoder parity, Renode check pass
- [ ] Enumerates as keyboard + mouse on a real PC
- [ ] Works with the real sensor; axis signs checked
- [ ] Built with `SWING_MODEL=ON` and gestures fire in Minecraft
- [ ] Numbers tuned and written to `docs/results.md`
