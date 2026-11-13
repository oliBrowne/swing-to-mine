# Driver setup: USB-UART adapter and ST-LINK

Before `host/record.py` can read the gyro, your computer needs to see **two USB devices**:

| Device | What it's for | Needs a driver? |
|---|---|---|
| **USB-UART adapter** (CH340, CP2102, FTDI or PL2303 chip) | Gyro data from board pins PA2/PA3 to the PC. This is the port you pass as `--port`. | **Depends on the chip and your OS.** Often no. See step 2. |
| **ST-LINK** (the board's mini-USB port) | Power and flashing the firmware | **Windows: yes** (comes with STM32CubeProgrammer). Mac/Linux: no driver, but Linux needs a permissions rule. See step 4. |

Short version: **plug the adapter in first and check if a port appears (step 3). Only install a driver if it doesn't.**

These steps are written so a person or a coding agent can follow them. Steps marked 🧑 need a person (clicking an installer, approving an admin prompt, or unplugging a cable).

---

## Quick start: Windows laptop + STM32F4 Discovery (our setup)

This is the path for a Windows 10/11 laptop (e.g. Eli's HP Victus). The rest of the doc has the details if a step fails.

1. **ST-LINK driver (needed on Windows, for flashing).**
   🧑 Install **STM32CubeProgrammer** (https://www.st.com/en/development-tools/stm32cubeprog.html, free ST account) and keep the driver option ticked in the installer. Plug the board's **mini-USB** (top of the board) into the laptop. In **Device Manager > Universal Serial Bus devices** it should show as "STM32 STLink" with no ⚠. If it has a ⚠, install ST's **STSW-LINK009** driver (Step 4 below) and replug.
2. **Flash the capture firmware** with STM32CubeProgrammer: connect with ST-LINK, open `capture.hex`, click *Download*, then press the board's black reset button. Details: [firmware/capture/README.md](../firmware/capture/README.md#flash).
3. **Plug in the USB-UART adapter** (wired to PA2/PA3/GND per [wiring.md](wiring.md)). The capture firmware sends gyro data out of those pins, **not** through the board's own USB, so the adapter is required.
4. **Check if Windows already gave it a COM port.** In PowerShell, from the repo folder:
   ```powershell
   pip install -r host/requirements.txt
   python -m serial.tools.list_ports -v
   ```
   - A port with a CH340 / CP210x / FTDI USB ID (table in Step 1 below) is listed → **no adapter driver needed**, go to 6.
   - Nothing new is listed, or Device Manager shows the adapter with a ⚠ under "Other devices" → go to 5.
   - Ignore any "STLink Virtual COM Port": that's the board, not the adapter.
5. **Install the adapter driver** for its chip (Step 1 below to identify it, Step 2 for the download page). 🧑 Run the installer, approve the admin prompt, replug the adapter, and run the list command again.
6. **Test** with the board lying still (use your port number):
   ```powershell
   python host/record.py --port COM5 --check
   ```
   Expect about 100 Hz, gravity about 1.00 g, gyro near 0 deg/s. If the port opens but no samples arrive, it's wiring, not drivers: see [wiring.md](wiring.md).

---

## Step 1. Find out which chip your adapter has

The chip name is usually printed on the adapter's board or on its listing (e.g. "CH340G", "CP2102", "FT232RL"). If you can't read it, plug the adapter in and check its USB ID:

**Any OS (easiest, works once Python deps are installed):**

```bash
pip install -r host/requirements.txt
python -m serial.tools.list_ports -v
```

Look at the `hwid` line, e.g. `USB VID:PID=1A86:7523`. If the adapter has no driver yet it won't be listed here, so use the OS command below instead.

**Windows (PowerShell):**

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\VID_*' } | Select-Object Status, FriendlyName, InstanceId
```

A device with Status `Error` or `Unknown` and no COM number is an adapter missing its driver. The `VID_xxxx&PID_xxxx` part of `InstanceId` is the USB ID.

**Mac:**

```bash
system_profiler SPUSBDataType
```

Look for "Vendor ID" and "Product ID" under the adapter.

**Linux:**

```bash
lsusb
```

Then match the USB ID:

| USB ID (VID:PID) | Chip | Name you'll usually see |
|---|---|---|
| `1A86:7523` (or other `1A86:...`) | **CH340 / CH341** (WCH) | "USB-SERIAL CH340", "USB2.0-Serial" |
| `10C4:EA60` | **CP2102 / CP210x** (Silicon Labs) | "CP210x UART Bridge" |
| `0403:6001`, `0403:6015` (other `0403:...`) | **FTDI** (FT232R, FT231X…) | "USB Serial Port", "FT232R USB UART" |
| `067B:2303` (or other `067B:...`) | **PL2303** (Prolific) | "Prolific USB-to-Serial Comm Port" |
| `0483:374B` / `0483:3748` | **ST-LINK** (the board itself, not the adapter) | "STM32 STLink", "ST-Link Debug" |

---

## Step 2. Does it need a driver?

| Chip | Windows 10/11 | Mac | Linux |
|---|---|---|---|
| **FTDI** | Usually installs by itself through Windows Update | Built in | Built in |
| **CP2102** | Often installs by itself; if not, install the Silicon Labs driver | Try first; install the Silicon Labs driver if no port appears | Built in |
| **CH340** | Often installs by itself on recent Windows; if not, install the WCH driver | Recent macOS has one built in; install the WCH driver if no port appears | Built in (Ubuntu: see the `brltty` fix below) |
| **PL2303** | Needs Prolific's driver. Cheap clone chips may refuse to work with it (see troubleshooting) | Install Prolific's driver | Built in |

"Built in" means: plug in and go to step 3.

Where to get the drivers (official vendor pages; download the one for your OS):

| Chip | Download page |
|---|---|
| CH340 | WCH: https://www.wch-ic.com/downloads/CH341SER_EXE.html (Windows), https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html (Mac) |
| CP2102 | Silicon Labs "CP210x VCP drivers": https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers |
| FTDI | FTDI "VCP drivers": https://ftdichip.com/drivers/vcp-drivers/ |
| PL2303 | Prolific's site (search "PL2303 driver Prolific"). If you have a choice, use a CH340 or CP2102 adapter instead. |

🧑 Installing a driver needs a person: run the installer, approve the admin prompt (Windows) or allow it in **System Settings > Privacy & Security** (Mac), then **unplug and replug the adapter**. A Mac may ask for a restart.

> Note for agents: don't download drivers from mirror or "driver updater" sites. Use the vendor pages above, and if a page has moved, search the vendor's own site.

---

## Step 3. Find the port name

Plug in **only the adapter** (not the ST-LINK yet) so there's no confusion, then:

**Any OS:**

```bash
python -m serial.tools.list_ports -v
```

| OS | What the adapter looks like | What to pass to `--port` |
|---|---|---|
| Windows | `COM3`, `COM5`, … | `--port COM5` |
| Mac | `/dev/cu.usbserial-1410`, `/dev/cu.wchusbserial1410`, `/dev/cu.SLAB_USBtoUART` | `--port /dev/cu.usbserial-1410` (use the `cu.` one, not `tty.`) |
| Linux | `/dev/ttyUSB0` | `--port /dev/ttyUSB0` |

Windows without Python: **Device Manager > Ports (COM & LPT)**. The adapter is the entry that appears when you plug it in and disappears when you unplug it.

**Unplug/replug test** (the surest way): run the list command, unplug the adapter, run it again. The port that vanished is yours.

**Linux only: permission to open the port.** Without this you get `Permission denied: '/dev/ttyUSB0'`:

```bash
sudo usermod -aG dialout $USER     # Arch/Manjaro: use "uucp" instead of "dialout"
```

🧑 Then log out and back in (or reboot).

**Don't use the ST-LINK's port.** The board's ST-LINK may also show up as a COM port ("STMicroelectronics STLink Virtual COM Port" on Windows, `/dev/ttyACM0` on Linux, `/dev/cu.usbmodem…` on Mac). That's not the adapter, and the capture firmware doesn't send data there.

Then check data is flowing (board flashed and wired per [wiring.md](wiring.md)):

```bash
python host/record.py --port COM5 --check
```

---

## Step 4. ST-LINK (for flashing the board)

| OS | What to do |
|---|---|
| **Windows** | 🧑 Install **STM32CubeProgrammer** from ST (https://www.st.com/en/development-tools/stm32cubeprog.html, free ST account needed). Its installer includes the ST-LINK USB driver. If you only want the driver: ST's **STSW-LINK009** (https://www.st.com/en/development-tools/stsw-link009.html). After install, the board shows up in Device Manager under **Universal Serial Bus devices** as "STM32 STLink" with no warning icon. |
| **Mac** | No driver needed. Install STM32CubeProgrammer, or `brew install open-ocd`. |
| **Linux** | No driver, but you need a udev rule or flashing fails with a permissions error. `sudo apt install openocd` installs one. Then 🧑 unplug and replug the board. |

Then flash as described in [firmware/capture/README.md](../firmware/capture/README.md#flash).

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Windows: adapter has a yellow ⚠ in Device Manager, or shows under "Other devices" | Driver missing. Find the chip (step 1), install its driver (step 2), replug. |
| Windows, PL2303: "PL2303HXA PHASED OUT SINCE 2012" or Code 10 | Prolific's newer drivers reject old and clone PL2303 chips. Easiest fix: use a CH340 or CP2102 adapter. |
| Ubuntu 22.04+: CH340 shows up in `lsusb` but `/dev/ttyUSB0` appears then vanishes | The `brltty` (braille display) service grabs it. `sudo apt remove brltty`, then replug. |
| Linux: `Permission denied` on `/dev/ttyUSB0` | Add yourself to `dialout` (step 3) and log out/in. |
| Mac: driver installed but no `/dev/cu.*` port | Approve it in **System Settings > Privacy & Security**, restart, replug. |
| Port appears but `--check` shows no samples | Not a driver problem: check TX/RX crossing and GND in [wiring.md](wiring.md). |
| `could not open port … Access is denied` (Windows) | Another program (serial monitor, another `record.py`) has the port open. Close it. |
| Nothing appears at all, on any OS | Try another USB cable. Some cables are charge-only and carry no data. |

---

## Checklist for a coding agent

On a Windows laptop with the Discovery board, the Quick start at the top is the same flow; this list is the general version.

1. Run `python -m serial.tools.list_ports -v`. If a port with a CH340/CP210x/FTDI/PL2303 USB ID (table in step 1) is listed, **no driver is needed**: skip to 5.
2. If not, run the OS command from step 1 to get the adapter's USB ID and identify the chip.
3. Use the step 2 table to decide if a driver is needed on this OS. If yes, give the person the vendor link from step 2 and ask them to install it and replug (you can download the installer, but they must approve it).
4. Re-run step 1's list command and confirm the port now appears.
5. On Linux, check `groups` includes `dialout`; if not, run the `usermod` command and tell the person to log out and back in.
6. Report the port name (e.g. `COM5`, `/dev/cu.usbserial-1410`, `/dev/ttyUSB0`) and run `python host/record.py --port <port> --check`.
7. For flashing, check step 4 for the ST-LINK on this OS.
