#!/usr/bin/env bash
# write_board.sh — flash mos-test1 to the Nucleo-F411RE and run it.
#
# Semihosting MUST be enabled before "reset run": the LED thread's
# os::trace::printf() issues BKPT 0xAB, which faults if semihosting is off.
# The USART2 console (115200 8N1) appears on the ST-Link VCP (/dev/ttyACM0);
# the "led on"/"led off" trace lines appear in this OpenOCD terminal.
#
# Usage: ./write_board.sh [path/to/firmware.elf]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

ELF="${1:-output/nucleo_f411re_mos_test1.elf}"
if [[ ! -f "$ELF" ]]; then
  echo "error: ELF not found: $ELF (run 'make' first)" >&2
  exit 1
fi

OCD="${OPENOCD:-$(ls -d "$HOME"/.local/xPacks/@xpack-dev-tools/openocd/*/.content/bin/openocd 2>/dev/null | sort -V | tail -1)}"
OCD="${OCD:-openocd}"
SCRIPTS_ARG=""
if [[ "$OCD" == *"/xPacks/"* ]]; then
  SCRIPTS_ARG="-s $(dirname "$(dirname "$OCD")")/openocd/scripts"
fi

exec "$OCD" $SCRIPTS_ARG \
  -f "$HERE/openocd.cfg" \
  -c "init" \
  -c "reset halt" \
  -c "program \"$ELF\"" \
  -c "arm semihosting enable" \
  -c "reset run"
