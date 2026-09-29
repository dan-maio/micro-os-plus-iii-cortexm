/**
 * @file  hw_result.hpp
 * @brief How a test ends a run with a verdict.
 *
 * The same helper the AArch32 and AArch64 ports carry.
 *
 * A test prints its RESULT line and then ends through the strong _Exit()
 * (src/semihosting-exit.cpp), which reports the verdict through semihosting
 * SYS_EXIT:
 *
 *   QEMU        the semihosting SYS_EXIT ends the process, and CTest reads the
 *               exit code.
 *   hardware    OpenOCD is attached and semihosting is enabled (every board's
 *               hw.sh issues `arm semihosting enable` before it resumes), so
 *               the SYS_EXIT stops the run instead of leaving the core
 *               spinning until the runner times out; the verdict is also the
 *               "RESULT: PASS|FAIL" line the runner greps.
 *
 * The idle loop that follows the call is therefore only reached when
 * semihosting is NOT available -- a bare build run without a debugger.
 *
 * Pattern in a test:
 *
 *     write_str ("RESULT: PASS\n");
 *     hw_result::ok ();                      // stops the run
 *     for (;;) sysclock.sleep_for (1000);    // never reached
 */
#pragma once

#include <cstdlib>

namespace hw_result
{
  // The board's console is the UART, but the runner (hw.sh) greps OpenOCD's
  // output, which only sees semihosting. Emit the verdict there too, so a
  // passing test is not reported as a timeout. Semihosting is already required
  // to end the run cleanly: _Exit() below is a semihosting SYS_EXIT.
  inline void __attribute__ ((always_inline))
  semi_write0 (const char* s)
  {
    register unsigned r0 __asm__ ("r0") = 0x04u; // SYS_WRITE0
    register const char* r1 __asm__ ("r1") = s;
    __asm__ volatile ("bkpt 0xAB"
                      :
                      : "r" (r0), "r" (r1)
                      : "r2", "r3", "ip", "lr", "memory", "cc");
  }

  // End the run reporting SUCCESS.
  inline void
  ok () noexcept
  {
    semi_write0 ("RESULT: PASS\n");
    std::_Exit (0);
  }

  // End the run reporting FAILURE.
  inline void
  fail () noexcept
  {
    semi_write0 ("RESULT: FAIL\n");
    std::_Exit (1);
  }
} // namespace hw_result
