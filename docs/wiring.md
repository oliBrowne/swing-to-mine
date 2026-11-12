# Wiring for data capture

How to hook up the MPU-6050 and a USB-to-serial (UART) adapter to the STM32F4 Discovery so `firmware/capture` can stream data to `host/record.py`.

The gyro and the UART adapter **don't connect to each other**. Each one wires to the Discovery separately.

```
  GY-521 (MPU-6050)            STM32F4 Discovery              USB-UART adapter
  -----------------            -----------------              ----------------
  VCC  ------------------------ 5V
  GND  ------------------------ GND ---------------------------- GND
  SCL  ------------------------ PB6
  SDA  ------------------------ PB9
                                PA2 (board TX) ----------------- RX
                                PA3 (board RX) ----------------- TX
                                                                 VCC: leave unconnected
                                Mini-USB (ST-LINK) -> PC         USB -> PC
```

## 1. MPU-6050 (GY-521 breakout) to the board, over I2C

| GY-521 pin | Discovery pin | Notes |
|---|---|---|
| VCC | **5V** | The GY-521 has its own 3.3 V regulator. The **3V** pin also works. |
| GND | **GND** | |
| SCL | **PB6** | I2C1 clock |
| SDA | **PB9** | I2C1 data |
| AD0 | leave unconnected (or GND) | Gives address `0x68` |
| XDA, XCL, INT | leave unconnected | Not used yet |

- The board's audio chip is also on PB6/PB9, but it has a different address (`0x4A`), so they share the pins without trouble.
- No extra resistors needed: the GY-521 has its own pull-ups.

## 2. UART adapter to the board

| Adapter pin | Discovery pin | Notes |
|---|---|---|
| RX | **PA2** (board TX) | TX and RX **cross over** |
| TX | **PA3** (board RX) | Not used by the firmware yet, but wire it anyway |
| GND | **GND** | Required: without a shared ground the data is garbage |
| VCC / 5V / 3V3 | **leave unconnected** | The board already gets power from its own USB |

- If the adapter has a **5V / 3.3V jumper**, set it to **3.3V**. The STM32 runs at 3.3 V.
- Serial settings: **115200 baud, 8 data bits, no parity, 1 stop bit** (8N1). That's the default in `host/record.py`.
- On Windows the adapter shows up as a COM port in **Device Manager > Ports (COM & LPT)**. That's the port to pass as `--port`. If it doesn't show up, install the driver for its chip (CH340, CP2102 or FTDI, printed on the adapter).

## 3. Power and flashing

- The **mini-USB** port (ST-LINK, top of the board) powers the board and is used to flash it.
- The UART adapter's USB goes to the PC too. So during capture there are **two USB cables**: ST-LINK for power, adapter for data.

## Cable length while swinging

The short jumper wires only need to reach from the board to the adapter. The cable that matters is USB to the PC.

1. **Now:** tape or rubber-band the UART adapter to the back of the board so they move together, and plug it into a **USB extension cable** (2–3 m). Do the same for the ST-LINK cable, or power the board from a USB power bank in your pocket.
2. **Later (planned, firmware M3):** stream over the board's own **micro-USB** port as USB serial. Then one long micro-USB cable carries power and data, and the UART adapter isn't needed.
3. **Optional:** a Bluetooth serial module (HC-05) on the same PA2/PA3 pins removes the data cable, but the board then needs a battery.

## Make it survive swinging

- Jumper wires pop off when you swing. Tape them down, or solder/hot-glue the GY-521 to the handle once the wiring works.
- **Mount the GY-521 the same way every session.** If the sensor shifts, the data changes.

## Check that it works

1. Flash `firmware/capture` (see [../firmware/capture/README.md](../firmware/capture/README.md)).
2. Run `python host/record.py --port COM5 --check` with the board lying still.
3. You should see about **100 Hz**, gravity about **1.00 g**, gyro near **0 deg/s**.

| Symptom | Likely cause |
|---|---|
| No samples at all | Wrong COM port; TX/RX not crossed; GND missing |
| Garbled characters | Wrong baud rate, or GND missing |
| `# error: mpu6050 not answering` and red LED on | SCL/SDA swapped or loose; GY-521 not powered |
| Gravity far from 1.00 g | Sensor not reading properly; check `who_am_i` printed at boot |
