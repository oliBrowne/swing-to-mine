# firmware/capture

Small stand-alone firmware for **recording training data**. It reads the MPU-6050 (accelerometer + gyro) at 100 Hz and sends one text line per sample out of the UART (PA2), in the stream format from [docs/architecture.md](../../docs/architecture.md#usb-serial-stream-data-mode):

```
# swing-to-mine v1 rate_hz=100 accel_fs_g=16 gyro_fs_dps=2000
t_ms,ax,ay,az,gx,gy,gz
12034,-112,2051,48,-3,12,7
```

Values are raw counts: accel 2048 per g, gyro 16.4 per °/s. Lines starting with `#` are info or errors.

This is a stopgap until the main firmware has USB serial (milestone M3). The PC side (`host/record.py`) doesn't care which one is sending, because the text format is the same.

**Status:** builds, and the startup and serial output were checked in an emulator (QEMU) with no sensor attached. **Not yet tested on the real board with the MPU-6050.**

## Wiring

See [docs/wiring.md](../../docs/wiring.md): GY-521 on PB6/PB9, UART adapter on PA2/PA3 (crossed), shared GND.

## LEDs

| LED | Meaning |
|---|---|
| Blue (briefly at power-on) | Starting up |
| Green blinking (about 1 per second) | Samples are flowing |
| Red on | MPU-6050 not answering (it retries every second) |

## Build

Needs only the Arm GCC compiler, CMake and Ninja. No ST HAL or CubeIDE: the code sets the chip's registers directly, with comments.

**Easiest on Windows: use WSL (Ubuntu)**

```bash
sudo apt install gcc-arm-none-eabi cmake ninja-build
cd firmware/capture
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-gcc-toolchain.cmake
cmake --build build
```

This makes `build/capture.bin`, `build/capture.hex` and `build/capture.elf` (about 2 KB of flash).

## Flash

Pick one:

- **STM32CubeProgrammer** (Windows/Mac GUI): connect with ST-LINK, open `build/capture.hex` (or `.bin` at address `0x08000000`), click *Download*.
- **OpenOCD**: `openocd -f board/stm32f4discovery.cfg -c "program build/capture.elf verify reset exit"`

Then press the black reset button.

## Then record

```bash
pip install -r host/requirements.txt
python host/record.py --port COM5 --check          # is it working?
python host/record.py --port COM5 --person P01 --session s01 --reps 10
```

See [host/README.md](../../host/README.md).

## Files

| File | What it is |
|---|---|
| `src/main.c` | Everything: clock, LEDs, UART, I2C, MPU-6050 setup and the 100 Hz loop |
| `src/startup.c` | What runs at power-on before `main()` |
| `stm32f407vg.ld` | Memory map (where code and variables go) |
| `CMakeLists.txt`, `arm-gcc-toolchain.cmake` | Build setup |

## Settings worth knowing

- CPU on its 16 MHz internal clock (the power-on default).
- I2C at 100 kHz; MPU-6050 low-pass filter ~42 Hz, sample rate 100 Hz, ±16 g, ±2000 °/s.
- One line per new sample: the firmware waits for the MPU-6050's "data ready" flag. If a clone chip never sets it, it falls back to reading every ~11 ms.
- `t_ms` is the board's own clock in milliseconds since reset.
