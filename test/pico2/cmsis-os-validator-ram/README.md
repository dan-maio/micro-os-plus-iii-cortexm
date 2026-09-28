# CMSIS-RTOS Validator (RAM-Resident Execution) on Raspberry Pi Pico 2 (RP2350)

This directory contains the RAM-resident variant of the CMSIS-RTOS validator test for the Raspberry Pi Pico 2 (RP2350, dual Cortex-M33).

Unlike the standard flash-based tests, `cmsis-os-validator-ram` is linked to execute **entirely from internal SRAM** (`0x20000000`) without writing anything to the external QSPI flash.

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
| `linker-ram.ld` | Dedicated linker script mapping all loadable sections (`.text`, `.rodata`, `.data`) to SRAM `0x20000000`. Marks `.bss`, `.heap`, `.stack` as `(NOLOAD)` and embeds Pico `.embedded_end_block` to keep the download size compact (~56 KB) and avoid OpenOCD work-area collisions at `0x20010000`. |
| `harness-suite.cpp` | Test registration stub including `../harness-suite.hpp` for `__wrap_os_main()` semihosting and UART verdict reporting. |
| `hw.sh` | Custom hardware test runner. Automates OpenOCD direct SRAM image loading, core 1 bootrom holding, core 0 VTOR/SP/PC configuration, semihosting monitoring, and clean exit. |
| `load-ram.cfg` | Standalone TCL script for interactive debugging or manual OpenOCD flashing. |

---

## 3. Linker Memory Layout (`linker-ram.ld`)

```
0x20000000 +-------------------------+  __vector_table / _entry_point
           | .text (.vectors, code)  |
           | .rodata                 |
           +-------------------------+
           | .data (initialized vars)|
0x2000DC5C +-------------------------+  __data_end__ (End of ELF FileSiz)
           | .bss (NOLOAD)           |
           +-------------------------+
0x20010000 | OpenOCD work-area       |  (Free for fast CRC verification)
           +-------------------------+
           | .heap (NOLOAD, 128 KB)  |
           +-------------------------+
           | .stack (NOLOAD, 32 KB)  |
0x20039A80 +-------------------------+  __StackTop
```

---

## 4. Boot & Execution Flow

1. **Reset Init**: OpenOCD connects via SWD, halts both cores, and resets peripherals.
2. **Load & Verify**: ELF binary is loaded directly into SRAM `0x20000000` via `load_image` and validated with `verify_image`.
3. **Core 1 Initialization**: Core 1 (`rp2350.cm1`) has semihosting enabled and is resumed, idling in bootrom wait until launched by the RTOS.
4. **Core 0 Initialization**:
   - Vector Table Offset Register (VTOR, `0xE000ED08`) set to `0x20000000`.
   - Main Stack Pointer (SP) set to `[0x20000000]` (`__StackTop`).
   - Program Counter (PC) set to `[0x20000004] & ~1` (`_reset_handler`).
   - Semihosting enabled.
5. **Resume**: Core 0 begins executing directly from RAM.
6. **Verdict**: The RTOS test suite runs, validates kernel objects, timers, threads, and semaphores, then writes `RESULT: PASS` via semihosting and exits.

---

## 5. Running the Test

### Via CTest (Micro-OS+ SMP Test Framework)
```bash
cd /home/dan/Work/micro-os-plus-iii-smp/tests/build/cortexm-pico2-cmake-gcc-release
ctest -V -R cortexm-pico2-cmsis-os-validator-ram-hwd
```

### Via Standalone Script
```bash
/home/dan/Work/micro-os-plus-iii-cortexm/test/boards/pico2/hw.sh cmsis-os-validator-ram
```
