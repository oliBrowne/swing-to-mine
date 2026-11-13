#!/usr/bin/env bash
# Flash the model test over the onboard ST-LINK with OpenOCD.
#   bash firmware/model_test/scripts/flash.sh [build/model_test.bin]
# From WSL, first attach the ST-LINK in Windows:  usbipd attach --wsl --busid <BUSID>
set -euo pipefail
BIN=${1:-$(dirname "$0")/../build/model_test.bin}
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "transport select hla_swd" \
  -c "program $BIN 0x08000000 verify reset exit"
