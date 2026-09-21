#!/usr/bin/env bash
# run-qemu.sh — boot the spi-pipeline C++ image under QEMU and stop when the
# app has delivered its verdict.
#
# The app prints `spi-pipeline: PASS` (or FAIL) and then parks forever, so a
# plain `qemu ... -kernel` run never exits by itself. This wrapper starts QEMU
# in the background, tails the merged log (UART = stdout, semihosting = stderr)
# and kills QEMU as soon as the verdict line appears. Eight iterations take
# about half a minute of emulated time.
#
# netduinoplus2 is an STM32F405 (superset of the F411). This board's console
# is USART1 = serial_hd(0), the FIRST -serial, so one `-serial stdio`.
#
# Usage: run-qemu.sh [path/to/firmware.elf] [output.log]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

ELF="${1:-output/weact_f411ce_spi_pipeline.elf}"
LOG="${2:-/tmp/weact-spi-pipeline-qemu.log}"
if [[ ! -f "$ELF" ]]; then
  echo "error: ELF not found: $ELF (run 'make' first)" >&2
  exit 1
fi

QEMU="${QEMU_ARM:-}"
if [[ -z "$QEMU" ]] && [[ -x "$HOME/.local/xPacks/@xpack-dev-tools/qemu-arm/9.2.4-1.1/.content/bin/qemu-system-arm" ]]; then
  QEMU="$HOME/.local/xPacks/@xpack-dev-tools/qemu-arm/9.2.4-1.1/.content/bin/qemu-system-arm"
fi
QEMU="${QEMU:-qemu-system-arm}"

# Keep the caller's own logfile untouched.
: > "$LOG"

# The app parks after its verdict, so wait for the verdict, then stop QEMU.
# QEMU writes straight to the log (no intermediate pipe, which would buffer
# the tail and hide the verdict from the poll). Timeout: even on a slow host
# eight iterations should not take two minutes.
"$QEMU" \
  -M netduinoplus2 \
  -display none \
  -monitor none \
  -serial stdio \
  -semihosting-config enable=on,target=native \
  -kernel "$ELF" > "$LOG" 2>&1 &
qemu_pid=$!

verdict=""
for _ in $(seq 1 240); do # 240 x 0.5 s = up to 120 s
  if ! kill -0 "$qemu_pid" 2>/dev/null; then
    break # qemu already exited
  fi
  if grep -aqE '^spi-pipeline: (PASS|FAIL)' "$LOG"; then
    verdict="$(grep -aE '^spi-pipeline: (PASS|FAIL)' "$LOG" | tail -1)"
    break
  fi
  sleep 0.5
done

# Stop qemu whatever happened (its console pipe may block it otherwise).
kill "$qemu_pid" 2>/dev/null || true
wait "$qemu_pid" 2>/dev/null || true

if [[ -z "$verdict" ]]; then
  echo "run-qemu.sh: no verdict within 120 s (see $LOG)" >&2
  exit 2
fi
echo "$LOG: $verdict"
