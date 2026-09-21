#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# hw.sh — run this board's tests on the Raspberry Pi Pico 2 (RP2350).
#
#     test/boards/pico2/hw.sh            list the tests
#     test/boards/pico2/hw.sh <app>      program it and run it
#
# OpenOCD only, over CMSIS-DAP. No GDB, and nothing redirected: this board
# traces over its own UART (bsp trace-uart.cpp), not over semihosting, so the
# console is the board's serial port and OpenOCD exits once the cores are
# running.
#
# TWO THINGS HERE ARE NOT COSMETIC, both learned on the board:
#
#   `reset init`, not `reset`. A bare reset does not re-run the RP2350
#   bootrom/XIP setup, so the freshly programmed image never boots.
#
#   Resume cm1 BEFORE cm0. With USE_SMP 0 the rp2350 target exposes two
#   targets and `reset init` halts both, while a single `resume` resumes only
#   the current one. A debug-halted core 1 will NOT boot from the software PSM
#   reset that launch_core1() issues, so the launch hangs forever waiting for
#   the bootrom readiness word. Resuming cm1 first leaves it in the bootrom
#   core-1 launch loop, where it answers the FIFO handshake normally.
#
# Env:  BUILD=<dir>     the CMake build directory  (default: test/build-pico2)
#       OPENOCD=<path>  a specific OpenOCD
# -----------------------------------------------------------------------------
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="${BUILD:-$ROOT/test/build-pico2}"

list_tests () {
  echo "tests built for pico2:"
  for e in "$BUILD"/test/*-hwd; do
    [[ -e "$e" ]] || { echo "  (none — build first)"; return; }
    echo "  $(basename "$e" -hwd)"
  done
}

APP="${1:-}"
if [[ -z "$APP" || "$APP" == "list" ]]; then
  list_tests
  exit 0
fi

ELF="$BUILD/test/${APP}-hwd"
if [[ ! -f "$ELF" ]]; then
  echo "hw.sh: no such test: $APP" >&2
  list_tests >&2
  exit 2
fi

OCD="${OPENOCD:-$(ls -d "$HOME"/.local/xPacks/@xpack-dev-tools/openocd/*/.content/bin/openocd 2>/dev/null | sort -V | tail -1)}"
OCD="${OCD:-openocd}"
SCRIPTS_ARG=()
if [[ "$OCD" == *"/xPacks/"* ]]; then
  SCRIPTS_ARG=(-s "$(dirname "$(dirname "$OCD")")/openocd/scripts")
fi

pkill -9 -f "openocd.*boards/pico2/openocd.cfg" 2>/dev/null || true

echo "== Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33 SMP) : $APP =="
exec "$OCD" "${SCRIPTS_ARG[@]}" \
  -f "$HERE/openocd.cfg" \
  -c "init" \
  -c "reset init" \
  -c "program \"$ELF\" verify" \
  -c "reset init" \
  -c "targets rp2350.cm1" \
  -c "resume" \
  -c "targets rp2350.cm0" \
  -c "resume" \
  -c "shutdown"
