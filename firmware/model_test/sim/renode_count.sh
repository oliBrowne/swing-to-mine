#!/usr/bin/env bash
# Simulated work of firmware/model_test: instructions per inference in Renode.
#
#   cmake -B build-sim -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-gcc-toolchain.cmake -DCMAKE_CXX_FLAGS=-DSIM_NO_DELAY
#   cmake --build build-sim
#   bash sim/renode_count.sh path/to/renode build-sim/model_test.elf [seconds]
#
# Build with -DSIM_NO_DELAY (no 50 ms busy wait between rounds), otherwise the
# count includes the wait. The count per inference includes a little loop work
# around the model (copying the 2.4 KB input, comparing the scores).
#
# This is a work count, not a time: the real board also spends cycles on flash
# wait states and pipeline stalls. See docs/compression-and-speed.md for how it
# is turned into an estimate, and why the board still has to be measured.
#
# Renode 1.15.3 downloads an SVD file for the STM32F4 when the platform loads.
# Offline, comment out the "ApplySVD" line in platforms/cpus/stm32f4.repl.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
RENODE=${1:-renode}
ELF=$(realpath "${2:-$HERE/../build-sim/model_test.elf}")
SECONDS_SIM=${3:-1}
sym() { arm-none-eabi-nm "$ELF" | awk -v s="$1" '$3 == s { print "0x" $1 }'; }
ARGS="\$elf=@$ELF; \$seconds=\"$SECONDS_SIM\";"
for v in g_magic g_stage g_error g_iterations g_method_pool_used g_max_abs_diff; do ARGS="$ARGS \$$v=$(sym $v);"; done
A=$(sym g_argmax)
for i in 0 1 2 3 4 5; do ARGS="$ARGS \$a$i=$(printf 0x%x $((A + 4 * i)));"; done
OUT=$(cd "$(dirname "$(command -v "$RENODE")")" && timeout 1800 "$RENODE" --disable-gui --console --plain \
  -e "$ARGS include @$HERE/renode_count.resc" 2>&1 | tr -d "\r" || true)
mapfile -t R < <(echo "$OUT" | awk '/STEP first/ { f = 1; next } /STEP end/ { exit } f && /^(0x[0-9a-fA-F]+|[0-9]+)$/ { print $1 }')
[ "${#R[@]}" -ge 8 ] || { echo "$OUT" | tail -20; echo "could not read the results"; exit 1; }
RUNS=$(( R[2] - R[0] )); INSTR=$(( R[3] - R[1] ))
[ $((R[4])) -eq $((0x600D5EED)) ] && [ $((R[5])) -eq 3 ] || { echo "model not running: stage=$((R[5])) error=$((R[6]))"; exit 1; }
[ "$RUNS" -gt 0 ] || { echo "no inference finished; run longer"; exit 1; }
echo "inferences=$RUNS instructions=$INSTR method_memory=$((R[7]))"
echo "instructions per inference: $(( INSTR / RUNS ))"
DIFF=$(python3 -c "import struct; print(f'{struct.unpack(\"<f\", struct.pack(\"<I\", ${R[8]}))[0]:.3f}')")
echo "top gesture per test window: $(for x in "${R[@]:9:6}"; do printf "%d " $((x)); done)   largest score difference vs the PC's int8 reference: $DIFF"
