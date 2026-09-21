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

Three boards, all one Cortex-M4F:

| board | part | applications |
|---|---|---|
| `nucleof411` | STM32F411RE | `mos-test1` |
| `weactf411` | STM32F411CE | `mos-test1`, `spi-pipeline` |
| `weactf412` | STM32F412RE | `mos-test1`, `uart-test1` |

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

## One CPU, and why that matters

`src/rtos/os-core.cpp` is upstream's Cortex-M port, carried over unmodified.
It is single-core, and on these three boards that is what runs: `OS_NCPU=1`,
the kernel's SMP branches compile out, and **no kernel patch is applied**.
That is exactly how the predecessor proved these boards, and it is what makes
them the witness that the SMP port contract is optional rather than assumed.

The predecessor also has a dual-core Cortex-M SMP core, in its `pico2` tree.
It is **not** portable Cortex-M SMP and cannot simply replace this file: its
kernel lock is the RP2350's SIO hardware spinlock 0, its IPI is the SIO
inter-core FIFO on IRQ 25, and its CPU id is `SIO_CPUID`. All three are
silicon, not ISA — so in this layout they belong to a board, the way the
Lyra's GIC-400 SGI and the Pi's do. `cortexm` will gain SMP by a board
arriving that supplies a lock and an IPI, not by `os-core.cpp` changing.
