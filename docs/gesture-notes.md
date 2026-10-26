# Gesture notes

Working notes on the control scheme and the gesture dataset. Anything marked **open** still needs a team decision.

## Control scheme

The board is the whole controller: no keyboard or mouse.

### Button (blue user button)

| Press | Action |
|---|---|
| Short press | Switch between **look mode** and **move mode** |
| Long press | Recalibrate (hold the board still for 2 s) |

The LEDs show the current mode (for example green = look, orange = move).

### Gestures (work in both modes)

| Gesture | Motion | Input sent |
|---|---|---|
| Swing sword | One sharp slash | Left click |
| Mine | Keep chopping | Hold left click |
| Place block | Punch forward | Right click |
| Jump | Hop, or flick up | `Space` |
| Swap item left / right | Twist wrist left / right | Scroll wheel up / down |
| Nothing | Any other motion | Nothing |

Sword vs. mine is the hardest pair: Minecraft sees both as a left click, so the model has to tell one slash from repeated chopping.

### Look mode: "air mouse"

- The gyro measures how far the board rotates, and the board sends that as mouse movement. Turn the board 10° right and the camera turns about 10° right, so it feels like pointing.
- The board never needs to know where the screen is, only how it turns. That avoids the "where is the player's eye relative to the board" problem.
- **Calibration:** on startup (and on a long press), hold the board still for 2 s so the firmware can measure the gyro's built-in drift and subtract it.
- **Swing fix:** a sword swing also rotates the board. Aiming pauses whenever the model detects a gesture, or when the board spins faster than a threshold, so the camera doesn't jerk.

### Move mode: **open**

| Option | How it works | Pros | Cons |
|---|---|---|---|
| **Tilt to walk** (suggested first) | Board acts as a joystick: tilt forward = walk, back = back up, sideways = strafe | Simple, reliable, no ML | Less impressive |
| Walk in place | March on the spot; a model detects "walking" from the bobbing and you walk where you're looking | Great demo, real ML | Needs a way to stop and to strafe |
| Point and go | Walk in whatever direction the board points | Intuitive | Hard to aim and move at once |

Plan: start with tilt-to-walk so the checkpoint has a working controller, then try walk-in-place as an ML stretch goal.

Extra gesture ideas: shake to open the inventory (`E`), quick drop of the board to crouch, faster walking to sprint.

## Gyro

We're using an **MPU-6050 (GY-521 breakout)**: cheap, very well documented, and it adds the rotation data that separates twist from punch and slash from chop. See [hardware.md](hardware.md) for parts and wiring.

- Set the gyro to its **±2000 °/s** range, because a hard swing can max out the lower ranges.
- Cheap GY-521 boards often use clone chips, so `WHO_AM_I` may not return `0x68`. Don't fail on it.
- Use the MPU's own accelerometer instead of the onboard LIS3DSH, so all 6 channels come from one chip at the same moment (one 14-byte I2C read at 100 Hz).
- **Mount it rigidly** (solder or hot glue). Jumper wires pop out when you swing the board, and a sensor that shifts between sessions changes the data.
- Possible upgrades: an ST LSM6DSOX / LSM6DS3 breakout (newer, lower noise) or a BNO085 (computes orientation for you, which makes aiming easier).

## Data collection plan

**Target: about 40 reps per gesture per person, from at least 4–5 people (about 200 examples per gesture).** For reference, the Lab 2 dataset used 140 per gesture from 7 people and reached about 95%. Our 6 channels should make these gestures easier to tell apart.

| Class | Per person | How to record |
|---|---|---|
| Swing sword | 40 | One slash per clip |
| Place block | 40 | One punch per clip |
| Jump | 40 | Mix hops and flicks |
| Swap left | 40 | Separate class from swap right |
| Swap right | 40 | Separate class from swap left |
| Mine | 20 clips | Chop continuously for 3–4 s per clip |
| **Nothing** | **about 10 minutes** | Aiming, tilt-walking, resting, scratching your head, sitting down |

That's about 25 minutes of recording per person, in one sitting.

### Why "nothing" matters most

The model runs constantly on live motion, so most of what it sees isn't a gesture. Without plenty of random everyday movement in the data, it will keep firing gestures by accident.

### Tips

- **Vary each gesture:** soft and hard, fast and slow, standing and sitting. Keep the **same grip** every time.
- **Test on someone new:** keep one person's data completely out of training and test on it. That shows the model works for a person it has never seen, which is what the live demo needs.
- **If it's only Oliver and Eli:** do about 60 reps each and lean on augmentation, but try to recruit 2–3 friends for 25 minutes each.
- Data comes off the board through the USB serial mode on the micro-USB port (no UART adapter needed).
