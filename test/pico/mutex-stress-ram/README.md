# Mutex Stress Test (RAM-Resident Execution) on Raspberry Pi Pico (RP2040)

This directory contains the RAM-resident variant of the mutex stress test suite for the Raspberry Pi Pico (RP2040, dual Cortex-M0+).

Like `cmsis-os-validator-ram`, this test is linked to execute **entirely from internal SRAM** (`0x20000000`) without writing anything to the external QSPI flash.

---

## 1. Overview & Architecture

- **Target Architecture**: Raspberry Pi Pico (RP2040, 2x Cortex-M0+).
- **Toolchain**: `arm-none-eabi-gcc` 15.2 (xPack GNU Arm Embedded Toolchain).
- **Debugger**: OpenOCD via the JTAGprobe (CMSIS-DAP v2, `2e8a:000c`), SWD at 15 MHz by default (`UOS_HW_ADAPTER_KHZ` overrides it).
- **Execution Target**: Internal SRAM (`0x20000000`, 264 KB).

### Isolation Guarantee
- All modifications are strictly isolated to this test directory and its test hook registrations.
- Existing flash-based tests, board-level OpenOCD configs, and framework boot flows remain completely unmodified.

---

## 2. Components

| File | Purpose |
|------|---------|
| `linker-ram.ld` | Dedicated linker script mapping all loadable sections (`.text`, `.rodata`, `.data`) to SRAM `0x20000000` (264 KB). Marks `.bss`, `.heap` and the 8 KB `.stack` as `(NOLOAD)` and discards `.boot2` (the flash second stage, not needed in RAM), so only the loadable sections are downloaded (~39 KB). |
| `harness-suite.cpp` | Test registration stub including `../harness-suite.hpp` for `__wrap_os_main()` semihosting and UART verdict reporting. |
| `hw.sh` | Custom hardware test runner. Sets the OpenOCD target work-area to the top of SRAM (`0x20041000`, size `0x1000`; used only by `verify_image`, before the run), automates direct SRAM image loading, core 1 bootrom holding, core 0 VTOR/SP/PC configuration, semihosting monitoring, and clean exit. |
| `load-ram.cfg` | Standalone TCL script for interactive debugging or manual OpenOCD flashing. |

---

## 3. Running the Test

### Via CTest (Micro-OS+ SMP Test Framework)
```bash
cd ~/Work2/micro-os-plus/micro-os-plus-iii/tests/build/cortexm-pico-cmake-gcc-debug
ctest -V -R cortexm-pico-mutex-stress-ram-hwd
```

### Via Standalone Script
```bash
BUILD=$PWD/platform-bin/port-tests ~/Work2/micro-os-plus/micro-os-plus-iii-cortexm/test/boards/pico/hw.sh mutex-stress-ram
```
