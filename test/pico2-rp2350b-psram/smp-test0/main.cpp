/*
 * smp-test0 (RP2350B PSRAM board) — Phase-1 bare bring-up (no RTOS yet).
 *
 * Proves the C++ boot chain and dual-core launch before the kernel goes on top:
 *   - RP2350 boot (PICOBIN) → _reset_handler → C++ static constructors → main()
 *   - clocks 150 MHz, UART0 console @115200, semihosting-free
 *   - a global object's constructor runs (proves .init_array)
 *   - core 0 prints its SIO_CPUID and a tick counter on the UART
 *   - core 1 is launched and blinks the onboard LED (GPIO25), independently
 *
 * This is the M33 analogue of the A7 smp_test0 boot check; the RTOS/SMP
 * scheduler is added in later phases (see ../PLAN.md).
 */

#include <bsp/rp2350.hpp>
#include <bsp/clocks.hpp>
#include <bsp/uart.hpp>
#include <bsp/led.hpp>
#include <bsp/multicore.hpp>

#include <hw_result.hpp>

#include <cstdint>

extern "C" std::uint32_t __vector_table[];

// smp-test0 links neither the kernel nor its libc, so it supplies the exit
// endpoint hw_result ends through itself, and the verdict line, directly over
// ARM semihosting (Cortex-M BKPT 0xAB). The board's hw.sh enables semihosting
// before the resume, so both are serviced by OpenOCD: the RESULT line reaches
// the runner's log (the UART alone does not) and SYS_EXIT ends the run.
namespace
{
  void
  semi_write0 (const char* s)
  {
    if (!uos_debug_host_attached ()) // OpenOCD gone: see debug-host.h
      return;
    register unsigned r0 __asm__ ("r0") = 0x04u;  // SYS_WRITE0
    register const char* r1 __asm__ ("r1") = s;
    __asm__ volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
  }

  [[noreturn]] void
  semi_exit (unsigned reason)
  {
    while (!uos_debug_host_attached ()) // OpenOCD gone: see debug-host.h
      {
      }
    register unsigned r0 __asm__ ("r0") = 0x18u;  // SYS_EXIT
    register unsigned r1 __asm__ ("r1") = reason;
    __asm__ volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
    for (;;)
      {
      }
  }
} // namespace

extern "C" void _exit (int code) __attribute__ ((noreturn));

extern "C" void
_exit (int code)
{
  // Angel reason codes: ADP_Stopped_ApplicationExit / _RunTimeError.
  semi_exit (code == 0 ? 0x20026u : 0x20023u);
}

// Trap faults visibly instead of the boot.S silent "b ." — tells a core-0
// fault (e.g. XIP stall when core 1's bootrom runs) apart from a spin-hang.
extern "C" void
HardFault_Handler (void)
{
  uart::write ("\n!! HARDFAULT on core 0\n");
  for (;;)
    {
    }
}

namespace
{
  // Proves the C++ static-constructor path (.init_array) runs before main().
  struct ctor_probe
  {
    volatile bool ran;
    ctor_probe () : ran (true) {}
  };
  ctor_probe g_ctor_probe;

  // Crude busy-wait (no RTOS/SysTick yet); ~ms at 150 MHz, ~4 cycles/iter.
  void
  busy_wait_ms (unsigned ms)
  {
    volatile unsigned long n = static_cast<unsigned long> (ms) * (150000000UL / 4000UL);
    while (n--)
      {
        __asm__ volatile ("nop");
      }
  }

  std::uint32_t core1_stack[512] __attribute__ ((aligned (16)));

  // Set by core 1 the instant it starts, so core 0 can confirm the launch.
  volatile std::uint32_t g_core1_alive = 0;
  volatile std::uint32_t g_core1_loops = 0;

  // Core 1: own its FPU, blink the LED. No UART, no talk to core 0.
  void
  core1_entry (void)
  {
    g_core1_alive = 0xC0FFEEU;                      // FIRST thing: prove we ran
    rp2350::reg (rp2350::PPB_CPACR) = 0x00F00000U; // enable CP10/CP11
    led::init ();
    for (;;)
      {
        led::on ();
        busy_wait_ms (300);
        led::off ();
        busy_wait_ms (300);
        g_core1_loops++;
      }
  }
}

extern "C" int
main (void)
{
  clocks::init ();
  uart::init ();

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico 2 / RP2350B - micro-os-plus-iii port\n");
  uart::write ("  smp-test0 : Phase 1 bare bring-up\n");
  uart::write ("=============================================\n");

  uart::write ("core id        : ");
  uart::put_char (static_cast<char> ('0' + rp2350::cpu_id ()));
  uart::write ("\n");
  uart::write ("static ctor    : ");
  uart::write (g_ctor_probe.ran ? "RAN" : "NOT RUN");
  uart::write ("\n");
  uart::write ("system clock   : 150 MHz\n");
  uart::write ("UART0          : 115200 8N1 (GPIO0=TX, GPIO1=RX)\n");

  uart::write ("\nlaunching core 1 (LED blink)...\n");
  multicore::launch_core1 (core1_entry,
                           reinterpret_cast<std::uint32_t> (core1_stack)
                               + sizeof (core1_stack));
  uart::write ("core 1 launch returned.\n\n");

  // A bounded run so the hardware runner gets a verdict: N ticks of ~500 ms
  // (about 10 s), then check that core 1 really came up before passing.
  constexpr unsigned long kIterations = 20;

  for (unsigned long tick = 0; tick < kIterations; ++tick)
    {
      uart::write ("[core0] tick 0x");
      uart::print_hex32 (tick);
      uart::write ("  core1_alive=0x");
      uart::print_hex32 (g_core1_alive);
      uart::write ("  core1_loops=0x");
      uart::print_hex32 (g_core1_loops);
      uart::write ("\n");
      busy_wait_ms (500);
    }

  const bool ok = (g_core1_alive == 0xC0FFEEU) && (g_core1_loops != 0U);
  uart::write ("\ncore 1 launch  : ");
  uart::write (ok ? "PASS" : "FAIL");
  uart::write ("\n");
  uart::write (ok ? "RESULT: PASS\n" : "RESULT: FAIL\n");
  // Also on the debug channel, which is what the hardware runner greps.
  semi_write0 (ok ? "RESULT: PASS\n" : "RESULT: FAIL\n");

  // Ends the run through the strong _Exit() -> semihosting SYS_EXIT, which the
  // board's hw.sh is waiting for (and which carries the verdict).
  if (ok)
    {
      hw_result::ok ();
    }
  else
    {
      hw_result::fail ();
    }
  for (;;)
    {
    }
  return 0;
}
