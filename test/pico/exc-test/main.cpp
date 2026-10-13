/*
 * exc-test — bare M0+ exception-entry probe (no RTOS).
 *
 * Isolates the RP2040's first-exception behavior: arm SysTick and PendSV with
 * trivial handlers and report whether their exception ENTRY works at all. The
 * RTOS smp-test1 faults at the first PendSV entry with PC=0 (INVSTATE) even
 * though the flash vector table is correct.
 *
 * Two phases:
 *   Phase A — thread mode using MSP (SPSEL=0): arm SysTick, count ticks.
 *   Phase B — switch to PSP (SPSEL=1) like the RTOS start(): arm PendSV.
 * This tells us whether any non-HardFault entry works, and whether the PSP
 * stack mode matters.
 */

#include <cmsis_device.h>

#include <bsp/clocks.hpp>
#include <bsp/uart.hpp>

#include <hw_result.hpp>

#include <cstdint>

// exc-test links no libc, so it supplies the exit endpoint hw_result ends
// through itself, and the verdict line, over ARM semihosting (Cortex-M BKPT
// 0xAB). The board's hw.sh enables semihosting before the resume, so both are
// serviced: the RESULT line reaches the runner's log and SYS_EXIT ends the run.
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
  semi_exit (code == 0 ? 0x20026u : 0x20023u);
}

namespace
{
  volatile std::uint32_t g_systick = 0;
  volatile std::uint32_t g_pendsv = 0;
  volatile std::uint32_t g_stage = 0;

  void
  report (void)
  {
    uart::write ("stage=0x");
    uart::print_hex32 (g_stage);
    uart::write (" systick=0x");
    uart::print_hex32 (g_systick);
    uart::write (" pendsv=0x");
    uart::print_hex32 (g_pendsv);
    uart::write ("\n");
  }
}

extern "C" void
HardFault_Handler (void)
{
  uart::write ("\n!! HARDFAULT (exc-test)\n");
  report ();
  for (;;)
    {
    }
}

extern "C" void
SysTick_Handler (void)
{
  g_systick++;
}

extern "C" void
PendSV_Handler (void)
{
  g_pendsv++;
}

extern "C" int
main (void)
{
  clocks::init ();
  uart::init ();

  uart::write ("\n=============================================\n");
  uart::write ("  exc-test : M0+ exception-entry probe (RP2040)\n");
  uart::write ("=============================================\n");
  uart::write ("VTOR=0x");
  uart::print_hex32 (*(volatile std::uint32_t*) 0xE000ED08);
  // The vector table follows boot2, at 0x10000100 (PendSV @+0x38,
  // SysTick @+0x3C).
  uart::write ("\nvec[38]=0x");
  uart::print_hex32 (*(volatile std::uint32_t*) 0x10000138);
  uart::write ("\nvec[3c]=0x");
  uart::print_hex32 (*(volatile std::uint32_t*) 0x1000013C);
  uart::write ("\n");

  // ---- Phase A: MSP thread mode, arm SysTick -----------------------------
  g_stage = 1;
  SysTick_Config (125000);   // 1 kHz at 125 MHz
  NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
  uart::write ("[A] SysTick armed (MSP mode)\n");

  for (volatile std::uint32_t n = 0; n < 1000000UL; ++n)
    {
      __asm__ volatile ("nop");
    }
  g_stage = 2;
  report ();

  // ---- Phase B: switch to PSP (like the RTOS), arm PendSV ----------------
  {
    std::uint32_t sp;
    __asm__ volatile ("mrs %0, msp" : "=r" (sp));
    __asm__ volatile ("msr psp, %0" : : "r" (sp));
    __asm__ volatile ("msr control, %0" : : "r" (0x02u));  // SPSEL=1
    __asm__ volatile ("isb");
  }

  g_stage = 3;
  NVIC_SetPriority (PendSV_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
  SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;   // PendSV pending
  uart::write ("[B] PENDSVSET set (PSP mode); pendsv should become 1\n");

  // Bounded run so the hardware runner gets a verdict: the point of the probe
  // is that BOTH exception entries actually ran.
  constexpr unsigned kIterations = 20;
  for (unsigned it = 0; it < kIterations; ++it)
    {
      for (volatile std::uint32_t n = 0; n < 2000000UL; ++n)
        {
          __asm__ volatile ("nop");
        }
      g_stage = 4;
      report ();
    }

  const bool ok = (g_systick != 0U) && (g_pendsv != 0U);
  uart::write ("\nexception entry : ");
  uart::write (ok ? "PASS" : "FAIL");
  uart::write ("\n");
  uart::write (ok ? "RESULT: PASS\n" : "RESULT: FAIL\n");
  // Also on the debug channel, which is what the hardware runner greps.
  semi_write0 (ok ? "RESULT: PASS\n" : "RESULT: FAIL\n");

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
