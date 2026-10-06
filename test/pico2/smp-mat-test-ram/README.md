# Parallel Block Matrix Solver (RAM-Resident Execution) on Raspberry Pi Pico 2 (RP2350)

This directory contains the RAM-resident variant of `smp-mat-test` for the Raspberry Pi Pico 2 (RP2350, dual Cortex-M33 SMP).

The test runs **entirely from internal SRAM** (`0x20000000`, 520 KB) without writing anything to the external QSPI flash.

---

## 1. Overview & Architecture

- **Target Architecture**: Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33 SMP).
- **Toolchain**: `arm-none-eabi-gcc` 15.2 (xPack GNU Arm Embedded Toolchain).
- **Debugger**: OpenOCD via CMSIS-DAP (Raspberry Pi Debug Probe).
- **Execution Target**: Internal SRAM (`0x20000000`, 520 KB).

### Test Operation
1. **Thread Pool Demo**: 10 workers, 20 tasks proving cross-core task distribution through semaphore + mutex pool.
2. **Parallel Block Elimination**: Block Gaussian elimination ($N=100, B=50$), row blocks distributed across the two Cortex-M33 cores, synchronized by SIO hardware spinlock barriers.
3. **Classical LU Solve**: Single-core LU solve as ground truth.
4. **Validation & Timing**: Compares both solutions, calculates speedup ratio, and validates precision.

---

## 2. Components

| File | Purpose |
|------|---------|
| `main.cpp` | Reuses the `smp-mat-test` parallel block solver implementation. |
| `linker-ram.ld` | Dedicated linker script mapping all loadable sections (`.text`, `.rodata`, `.data`) to SRAM `0x20000000`. Marks `.bss` (~266 KB), `.heap` (64 KB), and `.stack` (32 KB) as `(NOLOAD)` and embeds Pico `.embedded_end_block` to keep the download size compact (~32 KB). |
| `hw.sh` | Custom hardware test runner. Sets OpenOCD target work-area to high SRAM (`0x20070000`), automates direct SRAM image loading, core 1 bootrom holding, core 0 VTOR/SP/PC configuration, semihosting monitoring, and clean exit. |
| `load-ram.cfg` | Standalone TCL script for interactive debugging or manual OpenOCD flashing. |

---

## 3. Running the Test

### Via CTest (Micro-OS+ SMP Test Framework)
```bash
cd /home/dan/Work/micro-os-plus-iii-smp/tests/build/cortexm-pico2-cmake-gcc-release
ctest -V -R cortexm-pico2-smp-mat-test-ram-hwd
```

### Via Standalone Script
```bash
/home/dan/Work/micro-os-plus-iii-cortexm/test/boards/pico2/hw.sh smp-mat-test-ram
```
