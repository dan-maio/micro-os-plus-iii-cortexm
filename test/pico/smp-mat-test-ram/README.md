# Parallel Block Matrix Solver (RAM-Resident Execution) on Raspberry Pi Pico (RP2040)

This directory contains the RAM-resident variant of `smp-mat-test` for the Raspberry Pi Pico (RP2040, dual Cortex-M0+ SMP).

The test runs **entirely from internal SRAM** (`0x20000000`, 264 KB) without writing anything to the external QSPI flash.

---

## 1. Overview & Architecture

- **Target Architecture**: Raspberry Pi Pico (RP2040, 2x Cortex-M0+ SMP).
- **Toolchain**: `arm-none-eabi-gcc` 15.2 (xPack GNU Arm Embedded Toolchain).
- **Debugger**: OpenOCD via the JTAGprobe (CMSIS-DAP v2, `2e8a:000c`), SWD at 15 MHz by default (`UOS_HW_ADAPTER_KHZ` overrides it).
- **Execution Target**: Internal SRAM (`0x20000000`, 264 KB).

### Test Operation
1. **Thread Pool Demo**: 10 workers, 20 tasks proving cross-core task distribution through semaphore + mutex pool.
2. **Parallel Block Elimination**: Block Gaussian elimination ($N=100, B=50$), row blocks distributed across the two Cortex-M0+ cores, synchronized by SIO hardware spinlock barriers.
3. **Classical LU Solve**: Single-core LU solve as ground truth.
4. **Validation & Timing**: Compares both solutions, calculates speedup ratio, and validates precision.

---

## 2. Components

| File | Purpose |
|------|---------|
| `main.cpp` | Reuses the `smp-mat-test` parallel block solver implementation. |
| `linker-ram.ld` | Dedicated linker script mapping all loadable sections (`.text`, `.rodata`, `.data`) to SRAM `0x20000000` (264 KB). Marks `.bss`, `.heap` and the 8 KB `.stack` as `(NOLOAD)` and discards `.boot2` (the flash second stage, not needed in RAM), so only the loadable sections are downloaded (~48 KB). |
| `hw.sh` | Custom hardware test runner. Sets the OpenOCD target work-area to the top of SRAM (`0x20041000`, size `0x1000`; used only by `verify_image`, before the run), automates direct SRAM image loading, core 1 bootrom holding, core 0 VTOR/SP/PC configuration, semihosting monitoring, and clean exit. |
| `load-ram.cfg` | Standalone TCL script for interactive debugging or manual OpenOCD flashing. |

---

## 3. Running the Test

### Via CTest (Micro-OS+ SMP Test Framework)
```bash
cd ~/Work2/micro-os-plus/micro-os-plus-iii/tests/build/cortexm-pico-cmake-gcc-debug
ctest -V -R cortexm-pico-smp-mat-test-ram-hwd
```

### Via Standalone Script
```bash
BUILD=$PWD/platform-bin/port-tests ~/Work2/micro-os-plus/micro-os-plus-iii-cortexm/test/boards/pico/hw.sh smp-mat-test-ram
```
