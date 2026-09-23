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
  // End the run reporting SUCCESS.
  inline void
  ok () noexcept
  {
    std::exit (0);
  }

  // End the run reporting FAILURE.
  inline void
  fail () noexcept
  {
    std::exit (1);
  }
} // namespace hw_result
