/*
 * os-hooks.c — board glue between the shared micro-os-plus-iii Cortex-M
 * runtime and the Nucleo-F411RE.
 *
 * The kernel ships a weak, empty SysTick_Handler (so a stray SysTick cannot
 * crash a build that does not use it). This single-CPU RTOS build drives its
 * tick from the core SysTick, so we override it to call os_systick_handler(),
 * which advances the clock and requests a PendSV reschedule when needed.
 */

#include <cmsis-plus/rtos/os-c-decls.h>

#include "stm32f4xx.h"

/* Defined in the kernel (src/rtos/os-clocks.cpp). */
extern void
os_systick_handler (void);

/* ---- Startup hardware hooks --------------------------------------------- *
 * The shared kernel declares these weak but leaves the bodies #if 0'd, so the
 * board supplies them. _start() calls _early() before DATA/BSS init and the
 * plain one after, before the C++ constructors run.
 */

void
os_startup_initialize_hardware_early (void)
{
  /* Reset the RCC clock config to the default (HSI 16 MHz), set VTOR, and
     (on hard-float) enable the FPU. Leaves the core on HSI. */
  SystemInit ();
}

void
os_startup_initialize_hardware (void)
{
  /* Recompute SystemCoreClock from the (HSI) clock tree after RAM init. */
  SystemCoreClockUpdate ();
}

/* The core SysTick timer, programmed by the port's clock_systick::start()
   via SysTick_Config(SystemCoreClock / OS_INTEGER_SYSTICK_FREQUENCY_HZ). */
void __attribute__ ((section (".after_vectors")))
SysTick_Handler (void)
{
  os_systick_handler ();
}
