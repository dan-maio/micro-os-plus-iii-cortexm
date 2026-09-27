#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# hw.sh — run this board's tests on the Raspberry Pi Pico 2 (RP2350).
#
#     test/boards/pico2/hw.sh            list the tests
#     test/boards/pico2/hw.sh <app>      program it and run it
#
# OpenOCD only, over CMSIS-DAP. The console is the board's serial port
# (bsp trace-uart.cpp), so the test's own output is yours in your terminal; the
# RESULT line is also read here, from the copy teed to the log, so the script
# can report PASS/FAIL and stop.
#
# SEMIHOSTING IS ENABLED BEFORE THE RESUME, and that is not optional: the
# tests end through the strong _Exit() (test/pico2/include/hw_result.hpp),
# which is a semihosting SYS_EXIT. With semihosting off the BKPT is an
# unhandled debug event and the core halts; with it on the run ends and this
# script sees the verdict instead of waiting out the budget.
#
# THREE THINGS HERE ARE NOT COSMETIC, all learned on the board:
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
#   ONE TEST PER POWER CYCLE. Every run is a debug-in-RAM run; only a power
#   cycle puts the board back into a known state. Run one test, power-cycle,
#   run the next.
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
SECS="${2:-600}"

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

LOGS="$BUILD/.hw-logs"; mkdir -p "$LOGS"
LOG="$LOGS/${APP}.log"
rm -f "$LOG"

echo "== Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33 SMP) : $APP =="

# Run OpenOCD in the background so it stays attached after the resume: the
# test's semihosting SYS_EXIT has to reach a live debugger, and this script
# reads the verdict from the log as it appears.
"$OCD" "${SCRIPTS_ARG[@]}" \
  -f "$HERE/openocd.cfg" \
  -c "init" \
  -c "reset init" \
  -c "program \"$ELF\" verify" \
  -c "reset init" \
  -c "targets rp2350.cm1" \
  -c "arm semihosting enable" \
  -c "resume" \
  -c "targets rp2350.cm0" \
  -c "arm semihosting enable" \
  -c "resume" \
  > >(tee "$LOG") 2>&1 &
OCD_PID=$!

waited=0 rc=2
while (( waited < SECS )); do
  if grep -q 'RESULT: PASS' "$LOG" 2>/dev/null; then rc=0; break; fi
  if grep -q 'RESULT: FAIL' "$LOG" 2>/dev/null; then rc=1; break; fi
  # OpenOCD exiting on its own (bad probe, empty target list) is not a
  # timeout, and must not be reported as "TIMEOUT after 600s" for a run that
  # lasted a second. Classify it as run-hw.sh does.
  if ! kill -0 "$OCD_PID" 2>/dev/null; then rc=4; break; fi
  sleep 1; waited=$((waited + 1))
done

kill "$OCD_PID" 2>/dev/null; wait "$OCD_PID" 2>/dev/null

# OpenOCD's console is block-buffered when its stdout is a pipe, so a terse
# test's verdict can reach the log only when OpenOCD exits. Look once more now
# that it has, instead of reporting a timeout for a run that actually passed.
if [[ $rc -eq 2 || $rc -eq 4 ]]; then
  if grep -q 'RESULT: PASS' "$LOG" 2>/dev/null; then
    rc=0
  elif grep -q 'RESULT: FAIL' "$LOG" 2>/dev/null; then
    rc=1
  fi
fi

printf '\n%-24s ' "$APP"
case "$rc" in
  0) echo "PASS  (${waited}s)" ;;
  1) echo "FAIL  (see $LOG)" ;;
  4) echo "OPENOCD DIED  (see $LOG)" ;;
  *) echo "TIMEOUT after ${SECS}s  (see $LOG)" ;;
esac
echo "Power-cycle the board before the next test."
[[ "$rc" -eq 0 ]]
