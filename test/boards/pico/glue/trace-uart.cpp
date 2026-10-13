/*
 * trace-uart.cpp — route the kernel's trace::printf() to UART0 instead of ARM
 * semihosting. Overrides the weak os::trace::write() in the kernel's trace.cpp,
 * so the RTOS runs with no debugger/semihosting dependency (a semihosting BKPT
 * with no host faults the core).
 */

#include <cstdio>

#include <cmsis-plus/diag/trace.h>
#include <bsp/uart.hpp>

namespace os
{
  namespace trace
  {
    ssize_t
    write (const void* buf, std::size_t nbyte)
    {
      uart::write (static_cast<const char*> (buf), nbyte);
      return static_cast<ssize_t> (nbyte);
    }
  }
}

// Called by the kernel's semihosting __posix_write() (weak hook) for stdout
// and stderr: the console stream is written to the debugger's semihosting
// console and, here, mirrored to the board's UART, so the suites' printf()
// output (the per-thread statistics, etc.) is visible on the terminal. Kept
// alongside semihosting, not instead of it: routing the newlib path away from
// semihosting deadlocked OpenOCD on this SMP target.
extern "C" void os_board_console_mirror (int fildes, const void* buf,
                                         std::size_t nbyte);

extern "C" void
os_board_console_mirror (int /* fildes */, const void* buf, std::size_t nbyte)
{
  uart::write (static_cast<const char*> (buf), nbyte);
}

// stdout is not a tty as far as newlib is concerned, so it would be fully
// buffered and a suite's output would surface only when the buffer fills, or
// at exit -- too late, since the runner stops as soon as it reads RESULT. Line
// buffer it, so every printf() reaches the semihosting console and the UART as
// it is emitted.
namespace
{
  struct StdioLineBuffered
  {
    StdioLineBuffered ()
    {
      std::setvbuf (stdout, nullptr, _IOLBF, 0);
      std::setvbuf (stderr, nullptr, _IOLBF, 0);
    }
  };

  StdioLineBuffered stdio_line_buffered;
} // namespace
