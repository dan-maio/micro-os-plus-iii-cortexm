/**
 * @file  debug-host.h
 * @brief Is OpenOCD still attached, so that a semihosting BKPT is serviced?
 *
 * Every verdict a hardware test reports -- the SYS_WRITE0 of its RESULT line
 * and the SYS_EXIT that ends the run -- is a semihosting call, BKPT 0xAB,
 * which OpenOCD services over SWD (hw.sh: `arm semihosting enable`). With
 * OpenOCD attached the core halts on the BKPT, OpenOCD performs the request
 * and resumes it. With nothing attached there is nobody to halt for, and the
 * core escalates the BKPT to a HardFault (HFSR.DEBUGEVT, CFSR = 0).
 *
 * That happens on real boards: a test that resets the chip -- the RP2350
 * clock tests reboot into the band the potentiometer selects -- drops the
 * SWD session, and OpenOCD gives up on the target. The test then runs to
 * completion, prints "RESULT: PASS" on the UART, and faults on the
 * semihosting copy of the same line.
 *
 * DHCSR.C_DEBUGEN (0xE000EDF0, bit 0) is set by OpenOCD when it attaches and
 * cleared by the reset, so it answers the question exactly. It is read from
 * software on ARMv7-M and ARMv8-M, which are all the cores this port runs on
 * hardware (RP2350's Cortex-M33, the STM32F4s' Cortex-M4).
 *
 * ARMv6-M is the exception: the Cortex-M0+ (the RP2040, the pico board)
 * gives software no access to the debug registers, so DHCSR cannot answer
 * there and the probe reports "attached". The pico's hw.sh keeps OpenOCD
 * attached for the whole run, and none of its tests resets the chip.
 *
 * Only the `hwd` images ask (HW_BUILD). Under QEMU semihosting is serviced by
 * the emulator itself, with no debug state behind it, and QEMU does not
 * model DHCSR -- so a `qemu` image always answers "attached".
 */

#ifndef UOS_TEST_DEBUG_HOST_H_
#define UOS_TEST_DEBUG_HOST_H_

#include <stdint.h>

static inline int
uos_debug_host_attached (void)
{
#if defined(HW_BUILD) && !defined(__ARM_ARCH_6M__)
  return ((*(volatile uint32_t*)0xE000EDF0u) & 1u) != 0u; // DHCSR.C_DEBUGEN
#else
  return 1;
#endif
}

#endif // UOS_TEST_DEBUG_HOST_H_
