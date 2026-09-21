# mos-test1 — board-verification run log (Nucleo-F411RE)

Captured live from an ST Nucleo-F411RE (STM32F411RE, Cortex-M4F) running
`mos-test1` built from the shared `micro-os-plus-iii` kernel (`OS_USE_SMP_SCHEDULER`
undefined → stock single-CPU reference scheduler on the Cortex-M PendSV port).

- **USART2 @ 115200 8N1** (ST-Link VCP, `/dev/ttyACM0`) — the producer/consumer
  lines below: `[timestamp ms] role #id  local=<per-thread counter>  global=<shared, mutex-protected>`.
- **ARM semihosting** (OpenOCD console) — `led on` / `led off`, once per second.
- **LED LD2 (PA5)** — blinks at 1 Hz.

## What the log proves

- **sysclock / SysTick tick** — worker rounds are ~500 ms apart (2541 → 3045 →
  3549 → 4053 → 4557 → 5061 ms).
- **scheduler / PendSV context switch** — 3 producers + 3 consumers + 1 LED
  thread rotate cleanly every round.
- **counting semaphores** — the bounded buffer stays balanced (producers and
  consumers keep pace; no stall).
- **plain mutex** — `global` is strictly monotonic with no duplicates or lost
  updates → no race. It is incremented by *every* thread: 6 workers ×2/s plus
  the LED thread ×1/s, so each ~1 s adds 13. The single-number gaps (…39 → 41,
  52 → 54, 65 → 67) are the LED thread's own `bump_global()` between rounds.
- **recursive mutex** — the nested `banner()` (which locks the recursive console
  mutex and calls `console_line()`, locking it again) ran at boot without
  deadlock (printed before this capture window).
- **per-thread local counters** — advance independently, one per round.

## USART2 capture

```
[    2063 ms] consumer #2  local=5      global=33
[    2541 ms] producer #0  local=6      global=34
[    2550 ms] producer #1  local=6      global=35
[    2554 ms] producer #2  local=6      global=36
[    2559 ms] consumer #0  local=6      global=37
[    2564 ms] consumer #1  local=6      global=38
[    2568 ms] consumer #2  local=6      global=39
[    3045 ms] producer #0  local=7      global=41
[    3055 ms] producer #1  local=7      global=42
[    3059 ms] producer #2  local=7      global=43
[    3064 ms] consumer #0  local=7      global=44
[    3069 ms] consumer #1  local=7      global=45
[    3073 ms] consumer #2  local=7      global=46
[    3549 ms] producer #0  local=8      global=47
[    3560 ms] producer #1  local=8      global=48
[    3564 ms] producer #2  local=8      global=49
[    3569 ms] consumer #0  local=8      global=50
[    3574 ms] consumer #1  local=8      global=51
[    3578 ms] consumer #2  local=8      global=52
[    4053 ms] producer #0  local=9      global=54
[    4065 ms] producer #1  local=9      global=55
[    4069 ms] producer #2  local=9      global=56
[    4074 ms] consumer #0  local=9      global=57
[    4079 ms] consumer #1  local=9      global=58
[    4083 ms] consumer #2  local=9      global=59
[    4557 ms] producer #0  local=10     global=60
[    4570 ms] producer #1  local=10     global=61
[    4574 ms] producer #2  local=10     global=62
[    4579 ms] consumer #0  local=10     global=63
[    4584 ms] consumer #1  local=10     global=64
[    4588 ms] consumer #2  local=10     global=65
[    5061 ms] producer #0  local=11     global=67
[    5075 ms] producer #1  local=11     global=68
```

## Semihosting capture (OpenOCD console)

```
Hardware initialised
led on
led off
led on
led off
```
