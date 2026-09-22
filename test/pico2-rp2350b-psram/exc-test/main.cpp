/*
 * exc-test — bare M33 exception-entry probe (no RTOS).
 *
 * Isolates the RP2350's first-exception behavior: arm SysTick and PendSV with
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

#include <cstdint>

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

extern "C" void
UsageFault_Handler (void)
{
  uart::write ("\n!! USAGEFAULT (exc-test)\n");
  report ();
  for (;;)
    {
    }
}

extern "C" int
main (void)
{
  clocks::init ();
  uart::init ();

  uart::write ("\n=============================================\n");
  uart::write ("  exc-test : M33 exception-entry probe (RP2350B)\n");
  uart::write ("=============================================\n");
  uart::write ("VTOR=0x");
  uart::print_hex32 (*(volatile std::uint32_t*) 0xE000ED08);
  uart::write ("\nvec[3c]=0x");
  uart::print_hex32 (*(volatile std::uint32_t*) 0x1000003C);
  uart::write ("\nvec[40]=0x");
  uart::print_hex32 (*(volatile std::uint32_t*) 0x10000040);
  uart::write ("\n");

  // ---- Phase A: MSP thread mode, arm SysTick -----------------------------
  g_stage = 1;
  SysTick_Config (150000);   // 100 Hz
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

  for (;;)
    {
      for (volatile std::uint32_t n = 0; n < 2000000UL; ++n)
        {
          __asm__ volatile ("nop");
        }
      g_stage = 4;
      report ();
    }
  return 0;
}
