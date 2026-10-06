/**
 * @file semihosting-exit.cpp
 * @brief Strong _Exit() for the Cortex-M port.
 *
 * The same thing the AArch32 and AArch64 ports do, and for the same reason:
 * the kernel's _Exit() is weak (src/libc/stdlib/exit.c) and on a bare-metal
 * port would reset or idle, so returning from os_main() -- i.e.
 * std::exit(code) -- ends with the board spinning rather than with a result.
 * This strong definition terminates the run through semihosting SYS_EXIT, so
 * the run ends with a real exit code that OpenOCD reports.
 *
 * WHAT DIFFERS FROM THE A-PROFILE PORTS IS ONLY THE TRAP, and none of it is
 * written here. On ARMv7-M the semihosting trap is BKPT 0xAB, not SVC
 * 0x123456 and not HLT #0xF000 -- and the kernel already spells that, in
 * cmsis-plus/arm/semihosting.h, which picks the instruction from
 * __ARM_ARCH_7M__ / __ARM_ARCH_7EM__ / __ARM_ARCH_6M__. So this file calls
 * the kernel's report_exception() instead of carrying a second copy of the
 * trap. There is no semihosting.hpp in this port for the same reason: the
 * AArch32 one exists because the A-profile trap depends on which OpenOCD
 * target type is driving the core, a choice that does not arise here.
 *
 * The reason codes are the Angel ones the debugger expects:
 *   ADP_Stopped_ApplicationExit (0x20026)  success
 *   ADP_Stopped_RunTimeError    (0x20023)  anything else
 *
 * Semihosting must be enabled on the debugger before the image runs --
 * `arm semihosting enable`, which every board's hw.sh does before `reset
 * run`. Without it the BKPT is an unhandled debug event and the core halts.
 */

#if defined(SEMIHOST)

#include <cmsis-plus/arm/semihosting.h>

extern "C" void _Exit (int code) __attribute__ ((noreturn));

extern "C" void
_Exit (int code)
{
#if defined(HW_BUILD)
  // On hardware the BKPT is serviced only while OpenOCD is attached, which
  // DHCSR.C_DEBUGEN says. A test that reset the chip (the RP2350 clock tests)
  // has lost it: the BKPT would be a HardFault (HFSR.DEBUGEVT) right after
  // its UART "RESULT:" line. Park instead, and exit should OpenOCD re-attach.
  // The same check as the tests' debug-host.h, which this file cannot
  // include: it is built outside the test tree too. QEMU images (no
  // HW_BUILD) are unaffected -- the emulator services semihosting itself.
  while ((*reinterpret_cast<volatile unsigned*> (0xE000EDF0u) & 1u) == 0u)
    {
    }
#endif
  report_exception (code == 0 ? ADP_Stopped_ApplicationExit
                              : ADP_Stopped_RunTimeError);
}

#endif // defined(SEMIHOST)
