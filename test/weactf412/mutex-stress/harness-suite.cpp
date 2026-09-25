// A harness suite, run on the WeAct F412RE under the kernel's SMP scheduler
// at OS_NCPU=1.
//
// Its sources are not here: they live in the harness, tests/sources/<suite>/,
// and are named by board_test_libs() in tests.cmake. This file exists because
// the port's builder builds one application per directory that has a *.cpp,
// and the directory is where the application's NAME comes from, so that the
// platform registers <suite>-hwd like every other test. The same file serves
// all three suites (mutex-stress, rtos-apis, cmsis-os-validator).
//
// It also gives the suite the verdict a board run needs. A suite RETURNS its
// code from os_main(); hw.sh reads a RESULT line. The suite is linked with
// -Wl,--wrap=os_main (weactf412-harness-suite in tests.cmake), so the
// kernel's call to os_main() lands here: run the suite, print the line, and
// hand the code back to the kernel, whose std::exit() is the semihosting
// SYS_EXIT that ends the run.

#include <uart.hpp>

#include <cmsis-plus/rtos/os.h>

// hw.sh reads the verdict from OpenOCD's output, which only sees semihosting;
// the board's USART1 console (PA9/PA10) is for the terminal. Emit the RESULT
// line on both.
namespace
{
  void
  semi_write0 (const char* s)
  {
    register unsigned r0 __asm__ ("r0") = 0x04u; // SYS_WRITE0
    register const char* r1 __asm__ ("r1") = s;
    __asm__ volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
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

  // The suites print through semihosting and never bring the UART up. This
  // board's driver is DMA-driven: write() queues the line and returns, and
  // adds the '\r' itself. Give the DMA the few milliseconds the line takes
  // before std::exit() stops the core.
  uart::init (115200);
  uart::write (code == 0 ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
  os::rtos::sysclock.sleep_for (20);
  semi_write0 (code == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
  return code;
}
