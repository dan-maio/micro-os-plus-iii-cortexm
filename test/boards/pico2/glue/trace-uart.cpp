/*
 * trace-uart.cpp — route the kernel's trace::printf() to UART0 instead of ARM
 * semihosting. Overrides the weak os::trace::write() in the kernel's trace.cpp,
 * so the RTOS runs with no debugger/semihosting dependency (a semihosting BKPT
 * with no host faults the RP2350).
 */

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
