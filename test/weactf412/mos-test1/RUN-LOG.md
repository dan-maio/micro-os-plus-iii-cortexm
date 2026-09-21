# mos-test1 — board-verification run log (WeAct F412RE)

Template for the acceptance run on the WeAct Studio STM32F412RE 64-pin core
board, running `mos-test1` built from the shared `micro-os-plus-iii` kernel
(`OS_USE_SMP_SCHEDULER` undefined → stock single-CPU reference scheduler on the
Cortex-M PendSV port).

- **USART1 @ 115200 8N1** (PA9/PA10, USB-UART on the header) — the
  producer/consumer lines below:
  `[timestamp ms] role #id  local=<per-thread counter>  global=<shared, mutex-protected>`.
- **ARM semihosting** (OpenOCD console) — `led on` / `led off`, once per second.
- **LED (PB2)** — blinks at 1 Hz.

## What the log proves

- **sysclock / SysTick tick** — worker rounds are ~500 ms apart.
- **scheduler / PendSV context switch** — 3 producers + 3 consumers + 1 LED
  thread rotate cleanly every round.
- **counting semaphores** — the bounded buffer stays balanced (producers and
  consumers keep pace; no stall).
- **plain mutex** — `global` is strictly monotonic with no duplicates or lost
  updates → no race. It is incremented by *every* thread: 6 workers ×2/s plus
  the LED thread ×1/s, so each ~1 s adds 13.
- **recursive mutex** — the nested `banner()` (which locks the recursive
  console mutex and calls `console_line()`, locking it again) ran at boot
  without deadlock.
- **per-thread local counters** — advance independently, one per round.

## USART1 capture

```
(to be captured on the board: picocom -b 115200 /dev/ttyUSB0)
```

## Semihosting capture (OpenOCD console)

```
Hardware initialised
led on
led off
led on
led off
```
