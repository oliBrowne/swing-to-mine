"""Gesture classes for Swing to Mine.

Single source of truth for the class list and order. The firmware gets this
order through the export step. Change it only with the whole team, and update
docs/architecture.md in the same commit.
"""

CLASSES = [
    "nothing",     # any other motion: aiming, tilt-walking, resting
    "slash",       # horizontal swing      -> strike (click / hold)
    "chop",        # vertical chop         -> strike (click / hold)
    "punch",       # jab forward           -> right click (Shift+click in CURSOR)
    "hop",         # whole-body hop        -> Space
    "swap_left",   # first fast roll left  -> scroll up
    "swap_right",  # first fast roll right -> scroll down
    "shake",       # quick shake           -> E + toggle CURSOR mode
]

CLASS_TO_INDEX = {name: i for i, name in enumerate(CLASSES)}
