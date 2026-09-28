# RTOS APIs (RAM-Resident Execution) on Raspberry Pi Pico 2 (RP2350)

This directory contains the RAM-resident variant of the RTOS APIs test suite for the Raspberry Pi Pico 2 (RP2350, dual Cortex-M33).

Like `cmsis-os-validator-ram`, this test is linked to execute **entirely from internal SRAM** (`0x20000000`) without writing anything to the external QSPI flash.

---

## 1. Overview & Architecture

- **Target Architecture**: Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33).
- **Toolchain**: `arm-none-eabi-gcc` 15.2 (xPack GNU Arm Embedded Toolchain).
- **Debugger**: OpenOCD via CMSIS-DAP (Raspberry Pi Debug Probe).
- **Execution Target**: Internal SRAM (`0x20000000`, 520 KB).

### Isolation Guarantee
- All modifications are strictly isolated to this test directory and its test hook registrations.
- Existing flash-based tests, board-level OpenOCD configs, and framework boot flows remain completely unmodified.

---

## 2. Components

| File | Purpose |
|------|---------|
| `linker-ram.ld` | Dedicated linker script mapping all loadable sections (`.text`, `.rodata`, `.data`) to SRAM `0x20000000`. Marks `.bss`, `.heap`, `.stack` as `(NOLOAD)` and embeds Pico `.embedded_end_block` to keep the download size compact. |
| `harness-suite.cpp` | Test registration stub including `../harness-suite.hpp` for `__wrap_os_main()` semihosting and UART verdict reporting. |
| `hw.sh` | Custom hardware test runner. Sets OpenOCD target work-area to high SRAM (`0x20070000`), automates direct SRAM image loading, core 1 bootrom holding, core 0 VTOR/SP/PC configuration, semihosting monitoring, and clean exit. |
| `load-ram.cfg` | Standalone TCL script for interactive debugging or manual OpenOCD flashing. |

---

## 3. Running the Test

### Via CTest (Micro-OS+ SMP Test Framework)
```bash
cd /home/dan/Work/micro-os-plus-iii-smp/tests/build/cortexm-pico2-cmake-gcc-release
ctest -V -R cortexm-pico2-rtos-apis-ram-hwd
```

### Via Standalone Script
```bash
/home/dan/Work/micro-os-plus-iii-cortexm/test/boards/pico2/hw.sh rtos-apis-ram
```
