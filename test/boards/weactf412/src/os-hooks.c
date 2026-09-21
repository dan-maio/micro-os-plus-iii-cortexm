/*
 * os-hooks.c — board glue between the shared micro-os-plus-iii Cortex-M
 * runtime and the WeAct Studio STM32F412RE core board.
 *
 * The kernel ships a weak, empty SysTick_Handler (so a stray SysTick cannot
 * crash a build that does not use it). This single-CPU RTOS build drives its
 * tick from the core SysTick, so we override it to call os_systick_handler(),
 * which advances the clock and requests a PendSV reschedule when needed.
 *
 * Board bring-up (mirrors the Rust `clock` module): the WeAct STM32F412RE
 * core board carries an 8 MHz HSE crystal. The early hook leaves the FPU +
 * VTOR set-up to SystemInit(), then tries HSE -> PLL -> 100 MHz with the
 * AHB/APB dividers the reference firmware uses (AHB /1, APB1 /2, APB2 /1).
 *
 * The crystal readiness and PLL lock polls are BOUNDED (a few reads with a
 * spin between them), and every failure path simply leaves the core on the
 * reset HSI 16 MHz. That is what lets the same image boot under QEMU, whose
 * `netduinoplus2` machine models neither the crystal nor a locking PLL: it
 * stays on HSI there, exactly as the Rust port does. The plain hook runs
 * after DATA/BSS init, so it recomputes the CMSIS SystemCoreClock global
 * (whose 16 MHz initialiser was just copied back into RAM) from the live
 * RCC registers, which yields 100 MHz when the switch succeeded and 16 MHz
 * when it did not.
 */

#include <cmsis-plus/rtos/os-c-decls.h>

#include "stm32f4xx.h"

/* Defined in the kernel (src/rtos/os-clocks.cpp). */
extern void
os_systick_handler (void);

/* How many register reads we allow for a clock bit to appear, and how much
   spinning between reads. A few ms per poll interval at HSI speed — sized
   against the ~1–2 ms crystal stabilisation. */
#define READY_POLLS (16)
#define SPIN_BETWEEN_READS (20000U)

static int
wait_for_bit (volatile uint32_t* reg, uint32_t mask)
{
  for (int i = 0; i < READY_POLLS; ++i)
    {
      if ((*reg & mask) != 0U)
        {
          return 1;
        }
      for (uint32_t s = 0; s < SPIN_BETWEEN_READS; ++s)
        {
          __asm__ volatile ("" : : : "memory");
        }
    }
  return (*reg & mask) != 0U;
}

/* ---- Startup hardware hooks --------------------------------------------- *
 * The shared kernel declares these weak but leaves the bodies #if 0'd, so the
 * board supplies them. _start() calls _early() before DATA/BSS init and the
 * plain one after, before the C++ constructors run.
 */

void
os_startup_initialize_hardware_early (void)
{
  /* Reset the RCC clock config to the default (HSI 16 MHz), set VTOR, and
     enable the FPU. */
  SystemInit ();

  /* --- WeAct F412RE clock tree: HSE 8 MHz -> PLL -> 100 MHz ------------ *
   * Register-level equivalent of the Rust `clock::pll_to_100_mhz()`.
   * Only peripheral registers are touched here (no .data), which is safe
   * because DATA/BSS have not been initialised yet. Every wait is bounded;
   * on any failure we fall through with the core still on HSI.
   */

  /* Flash: 3 wait states (100 MHz @ 2.7-3.6 V), prefetch + I/D-cache on.
     Harmless to leave on even when the PLL never locks. */
  FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY)
             | FLASH_ACR_LATENCY_3WS
             | FLASH_ACR_PRFTEN
             | FLASH_ACR_ICEN
             | FLASH_ACR_DCEN;

  /* Enable HSE (bit 16) and wait for HSE ready (bit 17). */
  RCC->CR |= RCC_CR_HSEON;
  (void) RCC->CR;
  if (!wait_for_bit (&RCC->CR, RCC_CR_HSERDY))
    {
      return; /* no crystal (or none modelled): stay on HSI */
    }

  /* PLL: HSE / 8 * 200 / 2 = 100 MHz (PLLM=8, PLLN=200, PLLP=0 -> /2,
     PLLSRC=HSE, PLLQ=4). */
  RCC->PLLCFGR = (8U << RCC_PLLCFGR_PLLM_Pos)
               | (200U << RCC_PLLCFGR_PLLN_Pos)
               | (0U << RCC_PLLCFGR_PLLP_Pos)
               | (4U << RCC_PLLCFGR_PLLQ_Pos)
               | RCC_PLLCFGR_PLLSRC_HSE;
  (void) RCC->PLLCFGR;

  /* Enable PLL (bit 24) and wait for PLL ready (bit 25). */
  RCC->CR |= RCC_CR_PLLON;
  (void) RCC->CR;
  if (!wait_for_bit (&RCC->CR, RCC_CR_PLLRDY))
    {
      return; /* PLL never locked: stay on HSI */
    }

  /* AHB /1 (100 MHz), APB1 /2 (50 MHz), APB2 /1 (100 MHz). */
  RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
  RCC->CFGR |= (RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1);
  (void) RCC->CFGR;

  /* Switch the system clock source to PLL and wait until it takes effect. */
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  (void) RCC->CFGR;
  if (!wait_for_bit (&RCC->CFGR, RCC_CFGR_SWS_PLL))
    {
      return; /* switch never landed: stay on HSI */
    }
}

void
os_startup_initialize_hardware (void)
{
  /* Recompute SystemCoreClock from the live clock tree after RAM init:
     100 MHz if the PLL switch above landed, else HSI 16 MHz. */
  SystemCoreClockUpdate ();
}

/* The core SysTick timer, programmed by the port's clock_systick::start()
   via SysTick_Config(SystemCoreClock / OS_INTEGER_SYSTICK_FREQUENCY_HZ). */
void __attribute__ ((section (".after_vectors")))
SysTick_Handler (void)
{
  os_systick_handler ();
}
