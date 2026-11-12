# Hardware

## Parts list

| Part | Qty | Where | Notes |
|---|---|---|---|
| STM32F4 Discovery (STM32F407VG) | 1 | already have | Cortex-M4 @ 168 MHz, 1 MB flash, 192 KB RAM, LIS3DSH accelerometer |
| GY-521 MPU-6050 gyro + accelerometer breakout | 1 (packs have spares) | [Amazon: Ximimark 2-pack](https://www.amazon.com/dp/B07MMZ37PT) · backup: [AZDelivery 3-pack](https://www.amazon.com/dp/B07N2ZL34Z) | 3-axis gyro + 3-axis accelerometer, I2C, 3–5 V input |
| Female-to-female jumper wires | 4–5 | any | Board header pins to GY-521 pins |
| Micro-USB cable | 1 | any | Into the board's USB OTG port (CN5) for the HID controller |
| Mini-USB cable | 1 | already have | ST-LINK: power, flashing, debugging |
| USB-to-UART adapter (3.3 V logic: CH340, CP2102 or FTDI) | 1 | Eli has one | Data capture until USB serial works; see [wiring.md](wiring.md) |
| USB extension cable, 2–3 m | 1–2 | any | Room to swing while recording |

Any listing named **GY-521 MPU-6050** is the same module. The header pins usually ship loose, so they need a quick solder (or buy a pre-soldered one).

## Why the GY-521 works with the F407 Discovery

- **Voltage:** the GY-521 has its own 3.3 V regulator, so it runs from the Discovery's **5V** pin, and its I2C pull-ups go to that 3.3 V rail. That matches the F407's 3.3 V I/O.
- **I2C address:** `0x68` (AD0 pin low, the default). The only other device on the Discovery's I2C1 bus is the CS43L22 audio DAC at `0x4A`, so they can share the bus.
- **No conflict with the onboard accelerometer:** the LIS3DSH is on SPI1, not I2C.

## Wiring

| GY-521 pin | Discovery pin | Notes |
|---|---|---|
| VCC | 5V | Onboard regulator drops it to 3.3 V |
| GND | GND | |
| SCL | PB6 | I2C1 SCL (shared with the audio DAC) |
| SDA | PB9 | I2C1 SDA (shared with the audio DAC) |
| INT | optional | Any free GPIO with EXTI, for a data-ready interrupt |
| AD0, XDA, XCL | leave unconnected | AD0 low gives address `0x68` |

Full step-by-step wiring for data capture (gyro + USB-UART adapter, cable length tips, troubleshooting): [wiring.md](wiring.md).

## First check once it arrives

Read the `WHO_AM_I` register (`0x75`). It should return `0x68`. Then write `0x00` to `PWR_MGMT_1` (`0x6B`) to wake the chip, since it powers up asleep.
