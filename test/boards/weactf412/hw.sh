#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# hw.sh — run this board's tests on the WeAct F412RE.
#
#     test/boards/weactf412/hw.sh            list the tests
#     test/boards/weactf412/hw.sh <app>      program it and run it
#
# OpenOCD only. It programs the flash, enables semihosting and resets into the
# image; there is no GDB anywhere in this path, and nothing is redirected --
# the UART console is the board's own (115200 8N1, on the ST-Link's virtual
# COM port), and the semihosting output appears in this terminal.
#
# Semihosting MUST be enabled before "reset run": os::trace::printf() issues
# BKPT 0xAB, which faults if the debugger is not listening for it.
#
# Env:  BUILD=<dir>     the CMake build directory  (default: test/build-weactf412)
#       OPENOCD=<path>  a specific OpenOCD
# -----------------------------------------------------------------------------
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="${BUILD:-$ROOT/test/build-weactf412}"

list_tests () {
  echo "tests built for weactf412:"
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

echo "== WeAct F412RE : $APP =="
exec "$OCD" "${SCRIPTS_ARG[@]}" \
  -f "$HERE/openocd.cfg" \
  -c "init" \
  -c "reset halt" \
  -c "program \"$ELF\"" \
  -c "arm semihosting enable" \
  -c "reset run"
