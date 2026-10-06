/*
 * rp2350.h — minimal CMSIS device header for the RP2350 (Cortex-M33).
 *
 * Provides the IRQn_Type enumeration and the Cortex-M33 core configuration
 * that core_cm33.h requires, then pulls in core_cm33.h. This is the RP2350
 * analogue of a vendor CMSIS-Device header (e.g. stm32f411xe.h) — enough for
 * the micro-os-plus-iii Cortex-M port (SysTick_Config, NVIC_*, SysTick_IRQn,
 * PendSV_IRQn, __NVIC_PRIO_BITS).
 */

#ifndef PICO2_CMSIS_RP2350_H_
#define PICO2_CMSIS_RP2350_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Interrupt Number Definition --------------------------------------- */
typedef enum
{
  /* Cortex-M33 processor exceptions */
  NonMaskableInt_IRQn   = -14,
  HardFault_IRQn        = -13,
  MemoryManagement_IRQn = -12,
  BusFault_IRQn         = -11,
  UsageFault_IRQn       = -10,
  SecureFault_IRQn      = -9,
  SVCall_IRQn           = -5,
  DebugMonitor_IRQn     = -4,
  PendSV_IRQn           = -2,
  SysTick_IRQn          = -1,

  /* RP2350 external interrupts (subset; the port only needs a valid range) */
  TIMER0_IRQ_0_IRQn     = 0,
  SIO_IRQ_FIFO_IRQn     = 25,   /* inter-core FIFO — used as the SMP IPI later */
  UART0_IRQn            = 33,   /* PL011 UART0 (smp-test5: interrupt-driven TX) */
  /* RP2350 exposes 52 external IRQs (0..51). */
} IRQn_Type;

/* ---- Cortex-M33 processor and core peripheral configuration ------------ */
#define __CM33_REV                0x0000U   /* core revision r0p0 */
#define __NVIC_PRIO_BITS          4U        /* RP2350 uses 4 priority bits */
#define __Vendor_SysTickConfig    0U        /* use the CMSIS SysTick_Config */
#define __VTOR_PRESENT            1U
#define __MPU_PRESENT             1U
#define __FPU_PRESENT             1U        /* single-precision FPU */
#define __FPU_DP                  0U
#define __DSP_PRESENT             1U
#define __SAUREGION_PRESENT       1U        /* TrustZone SAU present */
#define __SAU_PRESENT             1U

#include "core_cm33.h"

/* ---- System clock (system_rp2350.c) ------------------------------------ */
extern uint32_t SystemCoreClock;

extern void
SystemInit (void);

extern void
SystemCoreClockUpdate (void);

#ifdef __cplusplus
}
#endif

#endif /* PICO2_CMSIS_RP2350_H_ */
