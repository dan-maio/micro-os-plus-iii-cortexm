#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# hw.sh - Hardware test runner for mutex-stress-ram on Pico (RP2040).
# Loads and executes directly in SRAM using OpenOCD without writing to flash.
# -----------------------------------------------------------------------------
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="${1:-$ROOT/test/build-pico}"
APP="${2:-mutex-stress-ram}"
SECS="${3:-600}"
# The JTAGprobe's SWD clock, as the board's hw.sh (default 15000 kHz).
KHZ="${UOS_HW_ADAPTER_KHZ:-15000}"

ELF="$BUILD/test/${APP}-hwd"
if [[ ! -f "$ELF" && -f "$BUILD/test/${APP}-hwd.elf" ]]; then
  ELF="$BUILD/test/${APP}-hwd.elf"
fi
if [[ ! -f "$ELF" ]]; then
  echo "hw.sh: no such test: $APP" >&2
  exit 2
fi

OCD="${OPENOCD:-$(ls -d "$HOME"/.local/xPacks/@xpack-dev-tools/openocd/*/.content/bin/openocd 2>/dev/null | sort -V | tail -1)}"
OCD="${OCD:-openocd}"
SCRIPTS_ARG=()
if [[ "$OCD" == *"/xPacks/"* ]]; then
  SCRIPTS_ARG=(-s "$(dirname "$(dirname "$OCD")")/openocd/scripts")
fi

pkill -9 -f "openocd.*boards/pico/openocd.cfg" 2>/dev/null || true

LOGS="$BUILD/.hw-logs"; mkdir -p "$LOGS"
LOG="$LOGS/${APP}.log"
rm -f "$LOG"

echo "== Raspberry Pi Pico (RP2040, Cortex-M0+ RAM Execution) : $APP  (SWD ${KHZ} kHz) =="

# Run OpenOCD in the background with direct RAM loading:
"$OCD" "${SCRIPTS_ARG[@]}" \
  -c "set ADAPTER_KHZ $KHZ" \
  -f "$ROOT/test/boards/pico/openocd.cfg" \
  -c "init" \
  -c "reset init" \
  -c "rp2040.core0 configure -work-area-phys 0x20041000 -work-area-size 0x1000" \
  -c "load_image \"$ELF\"" \
  -c "verify_image \"$ELF\"" \
  -c "targets rp2040.core1" \
  -c "arm semihosting enable" \
  -c "resume" \
  -c "targets rp2040.core0" \
  -c "arm semihosting enable" \
  -c "set v [read_memory 0x20000000 32 2]" \
  -c "mww 0xe000ed08 0x20000000" \
  -c "reg sp [lindex \$v 0]" \
  -c "reg pc [expr {[lindex \$v 1] & ~1}]" \
  -c "resume" \
  > >(tee "$LOG") 2>&1 &
OCD_PID=$!

waited=0 rc=2
while (( waited < SECS )); do
  if grep -q 'RESULT: PASS' "$LOG" 2>/dev/null; then rc=0; break; fi
  if grep -q 'RESULT: FAIL' "$LOG" 2>/dev/null; then rc=1; break; fi
  if ! kill -0 "$OCD_PID" 2>/dev/null; then rc=4; break; fi
  sleep 1; waited=$((waited + 1))
done

kill "$OCD_PID" 2>/dev/null; wait "$OCD_PID" 2>/dev/null

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
