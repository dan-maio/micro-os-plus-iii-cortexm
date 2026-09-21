/*
 * smp-test0 (Pico 2 / RP2350) — Phase-1 bare bring-up (no RTOS yet).
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

#include <cstdint>

extern "C" std::uint32_t __vector_table[];

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
  uart::write ("  Pico 2 / RP2350 - micro-os-plus-iii port\n");
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

  unsigned long tick = 0;
  for (;;)
    {
      uart::write ("[core0] tick 0x");
      uart::print_hex32 (tick++);
      uart::write ("  core1_alive=0x");
      uart::print_hex32 (g_core1_alive);
      uart::write ("  core1_loops=0x");
      uart::print_hex32 (g_core1_loops);
      uart::write ("\n");
      busy_wait_ms (500);
    }
  return 0;
}
