# Guide for AI assistants (and new teammates)

You're looking at **Swing to Mine**: a handheld STM32F4 Discovery + MPU-6050 that plays Minecraft (Java Edition) as a USB keyboard + mouse, with an int8 gesture model running on the chip. It's a CU Boulder Embedded AI final project (Fall 2026) by Oliver Browne and Eli Garcia.

**Status (Nov 23):** data capture works on hardware, the gesture model is trained and runs on the board (int8, 3.4 ms). The controller itself (USB keyboard/mouse, air mouse, button modes, in `firmware/swing_to_mine`) is written and builds with the model linked in; it is checked on a PC and in a simulator (including recorded gestures → model → clicks), but has **never run on a real board**. **Start with [docs/project-context.md](docs/project-context.md)**: it sums up the decisions, status, open PRs and team habits from our chats. Each folder's README says what's planned and what exists.

## Read in this order

0. [docs/project-context.md](docs/project-context.md): current status, decisions from chat, open PRs, how we work
1. [docs/requirements.md](docs/requirements.md): goals, must-haves, what's out of scope
2. [docs/architecture.md](docs/architecture.md): modules, modes, interfaces, timing, decisions log
3. [docs/roadmap.md](docs/roadmap.md): what to build this week
4. [docs/gesture-notes.md](docs/gesture-notes.md): gestures and data collection plan
5. [docs/controls.md](docs/controls.md): button, modes, crouch and inventory
6. [docs/council-review.md](docs/council-review.md): why the design looks the way it does
7. The README in the folder you're working in

## Ground rules

- **Keep it small.** The team's goal is a fun, working, post-worthy project by Thanksgiving (Nov 26), not a perfect one. Prefer the simplest thing that meets [requirements.md](docs/requirements.md). Don't add features or dependencies that aren't in the docs.
- **Don't change decided things silently.** Items marked ✅ in the decisions log are settled. If something should change, say so and update the log in the same change.
- **Respect the interfaces** in architecture.md (serial format, data file layout, class list, model input shape). Firmware, host and training are built in parallel and depend on them.
- **One source of truth for the class list:** `training/labels.py`. Firmware gets it through the export step, never by hand-copying.
- **No personal data in the repo:** people in the dataset are `P01`, `P02`, … Keep the name mapping elsewhere, and only commit data from people who agreed to it.
- **Measure, don't guess:** latency, flash/RAM use and false triggers per minute go into `docs/results.md` (create it when there are numbers) with how they were measured.

## Folder map

| Folder | What goes there | Owner (suggested) |
|---|---|---|
| `firmware/` | STM32 firmware (CMake + arm-none-eabi-gcc): sensor, USB, modes, inference. `git submodule update --init` for TinyUSB | Oliver |
| `host/` | PC scripts: record data, live plots, presentation overlay | Eli |
| `training/` | dataset, models, training, compression, export | Eli |
| `data/` | recorded sessions and labels | everyone |
| `docs/` | plans, decisions, results | everyone |

## Definition of done for any change

- It matches the docs, or the docs are updated in the same commit.
- The folder README's checklist is ticked.
- Anything with numbers says how they were measured.
