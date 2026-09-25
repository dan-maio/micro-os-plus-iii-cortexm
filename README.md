# micro-os-plus-iii-cortexm

The Cortex-M port of µOS++ III — the third architecture project, beside
[`micro-os-plus-iii-aarch32`](../micro-os-plus-iii-aarch32) and
[`micro-os-plus-iii-aarch64`](../micro-os-plus-iii-aarch64), built the same
way and from the same kernel.

## What is here

```
micro-os-plus-iii-cortexm/
├── CMakeLists.txt
├── include/cmsis-plus/rtos/port/    the port contract the kernel includes
├── src/rtos/os-core.cpp             the ISA: PendSV, SysTick, critical sections
└── test/
    ├── CMakeLists.txt  hw.sh  qemu.sh
    ├── boards/<id>/                 board.cmake, include/, src/, linker.ld,
    │                                openocd.cfg, hw.sh
    └── <id>/                        that board's test applications
```

Four boards, two Cortex-M profiles, one and two CPUs:

| board | part | CPUs | applications |
|---|---|---|---|
| `nucleof411` | STM32F411RE, Cortex-M4F | 1 | `mos-test1` |
| `weactf411` | STM32F411CE, Cortex-M4F | 1 | `mos-test1`, `spi-pipeline` |
| `weactf412` | STM32F412RE, Cortex-M4F | 1 | `mos-test1`, `uart-test1` |
| `pico2` | RP2350, 2× Cortex-M33 | **2** | `smp-test1`…`smp-test5`, `smp-mat-test` |

## Building

Clone the two siblings beside this one, then:

```sh
cmake -S . -B test/build -G Ninja -DBOARD=nucleof411 \
      -DCMAKE_TOOLCHAIN_FILE=../micro-os-plus-iii-smp/cmake/toolchains/arm-none-eabi.cmake
cmake --build test/build -j
```

## Running

Hardware only — all three boards are programmed over an ST-Link and run on
the desk. OpenOCD does the whole job: program, enable semihosting, reset run.
There is no GDB in this path and nothing is redirected.

```sh
BOARD=nucleof411 ./test/hw.sh          # list what is built
BOARD=nucleof411 ./test/hw.sh mos-test1
```

`test/qemu.sh` exists and answers honestly: none of these boards sets
`UOS_BOARD_LINKER_QEMU`, so each builds one image per test rather than two,
and the dispatcher points at `hw.sh`.

## SMP, and where it lives

`pico2` is the SMP board. Two identical, cache-coherent Cortex-M33 in one
coherency domain — unlike the RK3506's Cortex-M0 companion, these two are
peers, so the scheduler places threads on both and either can take an IPI
from the other.

Three pieces of RP2350 silicon make that work, and all three are the
**board's**, not the port's:

| | |
|---|---|
| kernel lock | SIO hardware spinlock 0 — not LDREX/STREX; the RP2350 has no global exclusive monitor, so the architectural route does not work across the two cores |
| IPI | the SIO inter-core FIFO on `SIO_IRQ_FIFO` (external IRQ 25); the handler drains the FIFO and pends its own core's PendSV |
| CPU index | `SIO_CPUID` |

Core 1 is launched through the bootrom FIFO handshake with its own MSP. Only
core 0 advances the RTOS clock; core 1's SysTick reschedules core 1 alone.

## One CPU, and why that matters

`src/rtos/os-core.cpp` is upstream's Cortex-M port, carried over unmodified.
It is single-core, and on these three boards that is what runs: `OS_NCPU=1`,
the kernel's SMP branches compile out, and **no kernel patch is applied**.
That is exactly how the predecessor proved these boards, and it is what makes
them the witness that the SMP port contract is optional rather than assumed.

`pico2` uses a different core — `src/rtos/os-core-rp2350.cpp`, with the port
headers that differ in `include-rp2350/`, searched before `include/` — and names it in its `board.cmake`
(`UOS_BOARD_PORT_CORE`, `UOS_BOARD_PORT_INCLUDE`). The two are 91% the same
file: pico2's is upstream's plus an `OS_USE_SMP_SCHEDULER` branch.

They are not merged today, for a stated reason rather than an oversight. The
three STM32 boards are hardware-proven on upstream's core, and moving them
onto a different 748-line scheduler has to be re-proven on the desk, not
assumed. A board therefore names the core it was proven with, and adding
`pico2` reached no STM32 board. Merging them is a later step.
