// ----------------------------------------------------------------------------
// smp-mat-test-ram - Parallel block linear solver running entirely in SRAM.
// Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33 SMP).
//
// Reuses the smp-mat-test parallel block Gaussian elimination implementation,
// linked for pure RAM execution (no flash writes), loaded and run directly
// via OpenOCD SWD.
// ----------------------------------------------------------------------------

#include "../smp-mat-test/main.cpp"
