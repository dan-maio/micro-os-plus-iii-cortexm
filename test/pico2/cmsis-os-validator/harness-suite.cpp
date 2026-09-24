// The harness suite "cmsis-os-validator".
//
// Its sources are not here: they live in the harness,
// tests/sources/cmsis-os-validator/, and are named by board_test_libs() in tests.cmake.
// This file exists because the port's builder builds one application per
// directory that has a *.cpp, and this directory is where the application's
// NAME comes from -- test/pico2/cmsis-os-validator/ -- so that the platform registers
// cmsis-os-validator-hwd and cmsis-os-validator-qemu like every other test.
//
// It also gives the suite the verdict a board run needs. A suite RETURNS its
// code from os_main(); hw.sh reads a RESULT line. The suite is linked with
// -Wl,--wrap=os_main (pico2-harness-suite in tests.cmake), so the kernel's
// call to os_main() lands here: run the suite, print the line the other pico2
// tests print, and hand the code back to the kernel, whose std::exit() is the
// same semihosting SYS_EXIT hw_result::ok()/fail() end with.

#include <bsp/uart.hpp>

// The board's console is the UART, but the harness (hw.sh) reads the verdict
// from OpenOCD's output, which only sees semihosting. Emit the RESULT line on
// both: the UART for the terminal, semihosting (BKPT 0xAB / SYS_WRITE0) for the
// runner. Semihosting is already required to end the run (std::exit below).
namespace
{
  void
  semi_write0 (const char* s)
  {
    register unsigned r0 __asm__ ("r0") = 0x04u;  // SYS_WRITE0
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
  uart::write (code == 0 ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
  semi_write0 (code == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
  return code;
}
