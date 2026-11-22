#!/usr/bin/env bash
# Simulator check of the built firmware (no hardware needed):
#   bash sim/renode_check.sh [path/to/renode]
# Needs Renode (https://renode.io, the portable Linux build works) and a build
# in ../build. Checks: boots, finds the (fake) sensor, calibrates into look
# mode (green), single press -> move (orange), hold 1 s -> inventory (blue),
# hold 1 s -> back to move, ~100 samples a second.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ELF="$HERE/../build/swing_to_mine.elf"
RENODE=${1:-renode}
[ -f "$ELF" ] || { echo "build the firmware first (no $ELF)"; exit 1; }
sym() { arm-none-eabi-nm "$ELF" | awk -v s="$1" '$3 == s { print "0x" $1 }'; }

OUT=$(cd "$(dirname "$(command -v "$RENODE")")" && timeout 300 "$RENODE" --disable-gui --console --plain \
  -e "\$elf=@$ELF; \$g_mode=$(sym g_mode); \$g_samples=$(sym g_samples); include @$HERE/renode_check.resc" 2>&1 | tr -d "\r" || true)
# the two numbers printed after each STEP line: mode, LED port
vals() { echo "$OUT" | awk -v s="STEP $1" '$0 ~ s { f = 1; next } f && /^0x/ { print; n++ } n == 2 { exit }' | tr '\n' ' '; }

fail=0
check() {   # step, expected mode, expected LED bits (GPIOD ODR), name
  read -r mode leds <<< "$(vals "$1")"
  leds=$(( ${leds:-0} & 0xF000 ))
  if [ "$((mode))" = "$2" ] && [ "$leds" = "$(( $3 ))" ]; then echo "PASS $4"
  else echo "FAIL $4: mode=$mode leds=$(printf 0x%X "$leds") (want mode $2, leds $3)"; fail=1; fi
}
# LEDs: green PD12 0x1000, orange PD13 0x2000, blue PD15 0x8000. Modes: 1 look, 2 move, 3 inventory.
check power-on        1 0x1000 "calibrated into look mode, green"
check single-press    2 0x2000 "single press -> move mode, orange"
check hold-1s         3 0x8000 "hold 1 s -> inventory, blue"
check hold-1s-again   2 0x2000 "hold 1 s again -> back to move, orange"
samples=$(echo "$OUT" | awk '/STEP samples/ { f = 1; next } f && /^0x/ { print; exit }')
if [ "$((samples))" -gt 700 ]; then echo "PASS $((samples)) sensor samples in ~8.5 s (~100 a second)"
else echo "FAIL only $((samples)) sensor samples"; fail=1; fi
exit $fail
