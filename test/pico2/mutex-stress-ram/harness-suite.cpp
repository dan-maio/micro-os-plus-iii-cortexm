// The harness suite "mutex-stress-ram" on the Pico 2 (RAM execution).
//
// Reuses the portable mutex-stress suite, linked to execute entirely
// from internal SRAM (no_flash), loaded and run directly via OpenOCD.

#include "../harness-suite.hpp"
