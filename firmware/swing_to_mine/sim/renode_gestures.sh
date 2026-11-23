#!/usr/bin/env bash
# End-to-end simulator check of the firmware WITH the gesture model:
# recorded motion -> fake MPU-6050 -> model on the simulated chip -> decoder
# -> controller -> what is handed to the USB code (clicks, scroll steps).
#
#   bash sim/renode_gestures.sh path/to/renode [recording.csv] [first_cue] [cues]
#
# Needs a SWING_MODEL=ON build in ../build-model (see the README), Renode, and
# for the PC reference the training env (torch). Default piece: P02 s10, cues
# 1-20. Renode runs this ~5-6x slower than real time (the model is the slow
# part), so 20 cues take ~6 minutes. Not in CI: CI has no ExecuTorch build.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
ELF="$HERE/../build-model/swing_to_mine.elf"
RENODE=${1:-renode}
CSV=${2:-$REPO/data/raw/P02/s10.csv}
FIRST=${3:-0}
CUES=${4:-20}
[ -f "$ELF" ] || { echo "build with -DSWING_MODEL=ON into build-model first (no $ELF)"; exit 1; }
[[ "$(arm-none-eabi-nm "$ELF")" == *model_pte* ]] || { echo "$ELF has no model in it"; exit 1; }
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

python3 "$HERE/make_replay.py" "$CSV" --first-cue "$FIRST" --cues "$CUES" -o "$WORK/piece"
ROWS=$(wc -l < "$WORK/piece.rows")
SECONDS_SIM=$(( ROWS / 100 + 2 ))

sym() { arm-none-eabi-nm "$ELF" | awk -v s="$1" '$3 == s { print "0x" $1 }'; }
ARGS="\$elf=@$ELF; \$rows=@$WORK/piece.rows; \$seconds=\"$SECONDS_SIM\";"
for v in g_mode g_samples g_inferences g_model_error g_model_errors g_out_left g_out_right g_out_left_ms \
         g_out_wheel g_out_wheel_steps; do ARGS="$ARGS \$$v=$(sym $v);"; done
EV=$(sym g_events)
for i in 0 1 2 3 4 5 6; do ARGS="$ARGS \$ev$i=$(printf 0x%x $((EV + 4 * i)));"; done

echo "simulating $SECONDS_SIM s ..."
OUT=$(cd "$(dirname "$(command -v "$RENODE")")" && timeout 3600 "$RENODE" --disable-gui --console --plain \
  -e "$ARGS include @$HERE/renode_gestures.resc" 2>&1 | tr -d "\r" || true)
nums() { echo "$OUT" | awk -v s="STEP $1" '$0 ~ s { f = 1; next } f && /^STEP/ { exit } f && /^(0x|[0-9])/ { print }'; }
mapfile -t R < <(nums result)
mapfile -t E < <(nums events)
[ "${#R[@]}" -ge 11 ] && [ "${#E[@]}" -ge 7 ] || { echo "FAIL: Renode output not understood"; echo "$OUT" | tail -20; exit 1; }
d() { echo $(( $1 )); }
s32() { local x=$(( $1 )); [ $x -ge 2147483648 ] && x=$(( x - 4294967296 )); echo $x; }

echo "rows played $(d "${R[0]}") of $ROWS, samples $(d "${R[2]}"), model runs $(d "${R[3]}")," \
     "model_init error $(d "${R[4]}"), failed runs $(d "${R[5]}"), mode $(d "${R[1]}")"
FW=$(printf '{"sword": %d, "place": %d, "swap_left": %d, "swap_right": %d, "mine": %d}' \
     $(d "${E[1]}") $(d "${E[2]}") $(d "${E[3]}") $(d "${E[4]}") $(d "${E[5]}"))
echo "cued:              $(cat "$WORK/piece.cues")"
echo "firmware fired:    $FW  (mine stops: $(d "${E[6]}"))"
echo "sent to USB:       left clicks $(d "${R[6]}") (left held $(d "${R[8]}") ms), right clicks $(d "${R[7]}")," \
     "scroll steps $(d "${R[10]}") (sum $(s32 "${R[9]}"), + = left)"
if python3 -c "import torch" 2>/dev/null; then
  echo "PC (float model):  $(python3 "$HERE/expected_events.py" "$WORK/piece.rows")"
fi

fail=0
ok() { if eval "$1"; then echo "PASS $2"; else echo "FAIL $2"; fail=1; fi; }
ok "[ $(d "${R[4]}") -eq 0 ] && [ $(d "${R[5]}") -eq 0 ]" "model loads and every run succeeds"
ok "[ $(d "${R[3]}") -ge $(( ($(d "${R[2]}") - 100) / 5 - 2 )) ]" "model runs every 5 samples ($(d "${R[3]}") runs)"
ok "[ $(d "${R[0]}") -ge $ROWS ]" "whole recording played"
ok "[ $(d "${E[1]}") -gt 0 ] && [ $(d "${E[5]}") -gt 0 ]" "gestures fire"
ok "[ $(d "${R[6]}") -eq $(( $(d "${E[1]}") + $(d "${E[5]}") )) ]" "every sword and mine reached USB as a left press"
ok "[ $(d "${R[7]}") -eq $(d "${E[2]}") ]" "every place reached USB as a right click"
ok "[ $(d "${R[10]}") -eq $(( $(d "${E[3]}") + $(d "${E[4]}") )) ] && [ $(s32 "${R[9]}") -eq $(( $(d "${E[3]}") - $(d "${E[4]}") )) ]" \
   "every swap reached USB as one scroll step the right way"
exit $fail
