// The harness suite "cmsis-os-validator-ram" on the Pico 2 (RAM execution).
//
// Reuses the portable CMSIS RTOS validator suite, linked to execute entirely
// from internal SRAM (no_flash), loaded and run directly via OpenOCD.

#include "../harness-suite.hpp"
