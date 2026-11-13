"""Gesture classes for Swing to Mine.

Single source of truth for the class list and order. The firmware gets this
order through the export step. Change it only with the whole team, and update
docs/architecture.md in the same commit.
"""

CLASSES = [
    "nothing",     # any other motion: slow pointing/looking, resting, fidgeting
    "sword",       # side swipe, board stays upright     -> one left click
    "mine",        # arched pickaxe swing forward + down -> hold left click
    "place",       # straight jab forward, no tilt       -> right click
    "swap_left",   # quick hotbar flick left             -> scroll up
    "swap_right",  # quick hotbar flick right            -> scroll down
]

# Look mode only. Jump (move mode) and the movement tilts are handled by simple
# rules in the firmware, not the model, so they are not recorded or trained.

# Names used in recordings made before Nov 13, mapped to the current names.
# Old "hop" and "shake" labels are not gestures any more. Do not count them as
# "nothing" either: training should skip those windows.
OLD_NAMES = {"slash": "sword", "chop": "mine", "punch": "place"}
DROPPED_OLD_NAMES = {"hop", "shake"}

CLASS_TO_INDEX = {name: i for i, name in enumerate(CLASSES)}
