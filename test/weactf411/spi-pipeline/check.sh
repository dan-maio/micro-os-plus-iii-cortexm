#!/usr/bin/env bash
# check.sh — verify a spi-pipeline transcript (C++ port) against the gates.
#
# Usage:
#   check.sh run-qemu.log          # expects the polled (RAM) fallback
#   check.sh run-board.log dma     # expects the DMA transport on silicon
#
# The same assertions as the Rust port's gate, applied to this transcript:
#   - PASS is the last line
#   - exactly ITERATIONS (8) iteration-summary lines
#   - the six `pipe N:` verdicts are all present
#   - the transport line names the expected one
#   - Thread 4 saw at least 3 distinct snapshot file lengths (the observer
#     really watched a partial file grow — PRODUCE_SPAN is load-bearing)
set -euo pipefail

log="${1:?usage: check.sh <log> [expected-transport]}"
want_transport="${2:-polled}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The console is a UART, so lines end in \r\n; strip the \r so anchored
# patterns and `$` work.
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
tr -d '\r' < "$log" > "$tmp"
log="$tmp"

failures=0
note() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }

# --- PASS / FAIL ------------------------------------------------------------
if ! grep -q '^spi-pipeline: PASS$' "$log"; then
  note "no 'spi-pipeline: PASS' line"
fi

# --- iteration summary lines ------------------------------------------------
iters=$(grep -cE '^\[ *[0-9]+ ms\] iter [0-9]+: [0-9]+ chunks, ' "$log" || true)
if [[ "$iters" -ne 8 ]]; then
  note "expected 8 iteration-summary lines, saw $iters"
fi

# --- the six verdicts -------------------------------------------------------
for p in 1 2 3 4 5 6; do
  if ! grep -qE "^pipe $p: " "$log"; then
    note "missing 'pipe $p:' verdict"
  fi
done

# --- transport (probed, not configured) ------------------------------------
if ! grep -qE "^spi-pipeline: SPI transport $want_transport " "$log"; then
  note "expected 'SPI transport $want_transport', got: $(grep -m1 'SPI transport' "$log" || true)"
fi

# --- JEDEC + backend --------------------------------------------------------
if ! grep -q '^spi-pipeline: JEDEC ' "$log"; then
  note "missing JEDEC line"
fi

# --- the observer really saw a partial file --------------------------------
lens=$(grep -oE 'cycle [0-9]+: [0-9]+ bytes' "$log" | awk '{ print $3 }' | sort -un | wc -l)
if [[ "$lens" -lt 3 ]]; then
  note "only $lens distinct snapshot lengths; PRODUCE_SPAN may be broken"
fi

# --- CRC triplet on semihosting ---------------------------------------------
if ! grep -q '^CRC0 = 0x' "$log" || ! grep -q '^CRC1 = 0x' "$log" || ! grep -q '^OK$' "$log"; then
  note "missing semihost CRC triplet"
fi

if [[ "$failures" -eq 0 ]]; then
  echo "check.sh: PASS ($(basename "$log"), transport $want_transport)"
else
  echo "check.sh: FAIL, $failures"
  exit 1
fi
