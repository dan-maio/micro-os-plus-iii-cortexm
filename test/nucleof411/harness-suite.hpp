// The verdict wrapper for the harness suites on the Nucleo-F411RE -- one copy
// for the three (mutex-stress, rtos-apis, cmsis-os-validator), included by
// each suite's test/nucleof411/<suite>/harness-suite.cpp.
//
// It gives each suite the verdict a board run needs. A suite RETURNS its
// code from os_main(); hw.sh reads a RESULT line. The suite is linked with
// -Wl,--wrap=os_main (nucleof411-harness-suite in tests.cmake), so the
// kernel's call to os_main() lands here: run the suite, print the line, and
// hand the code back to the kernel, whose std::exit() is the semihosting
// SYS_EXIT that ends the run.

#pragma once

#include <uart.hpp>

// hw.sh reads the verdict from OpenOCD's output, which only sees semihosting;
// the ST-Link's virtual COM port (USART2) is for the terminal. Emit the RESULT
// line on both.
namespace
{
  void
  semi_write0 (const char* s)
  {
    register unsigned r0 __asm__ ("r0") = 0x04u; // SYS_WRITE0
    register const char* r1 __asm__ ("r1") = s;
    __asm__ volatile ("bkpt 0xAB"
                      :
                      : "r" (r0), "r" (r1)
                      : "r2", "r3", "ip", "lr", "memory", "cc");
  }
} // namespace

extern "C" int
__real_os_main (int argc, char* argv[]);

extern "C" int
__wrap_os_main (int argc, char* argv[]);

extern "C" int
__wrap_os_main (int argc, char* argv[])
{
  int code = __real_os_main (argc, argv);

  // The suites print through semihosting and never bring the UART up; the
  // board's driver is polled, so starting it here is enough for one line.
  uart::init (115200);
  uart::write (code == 0 ? "\r\nRESULT: PASS\r\n" : "\r\nRESULT: FAIL\r\n");
  semi_write0 (code == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
  return code;
}
