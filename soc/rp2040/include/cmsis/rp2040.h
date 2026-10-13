/*
 * rp2040.h — minimal CMSIS device header for the RP2040 (2x Cortex-M0+).
 *
 * Provides the IRQn_Type enumeration and the Cortex-M0+ core configuration
 * that core_cm0plus.h requires, then pulls in core_cm0plus.h. The RP2040
 * analogue of rp2350.h: enough for the micro-os-plus-iii Cortex-M port
 * (SysTick_Config, NVIC_*, SysTick_IRQn, PendSV_IRQn, __NVIC_PRIO_BITS).
 * Written from the RP2040 datasheet; no Pico SDK.
 */

#ifndef PICO_CMSIS_RP2040_H_
#define PICO_CMSIS_RP2040_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Interrupt Number Definition --------------------------------------- */
typedef enum
{
  /* Cortex-M0+ processor exceptions */
  NonMaskableInt_IRQn   = -14,
  HardFault_IRQn        = -13,
  SVCall_IRQn           = -5,
  PendSV_IRQn           = -2,
  SysTick_IRQn          = -1,

  /* RP2040 external interrupts (datasheet 2.3.2) */
  TIMER_IRQ_0_IRQn      = 0,
  TIMER_IRQ_1_IRQn      = 1,
  TIMER_IRQ_2_IRQn      = 2,
  TIMER_IRQ_3_IRQn      = 3,
  PWM_IRQ_WRAP_IRQn     = 4,
  USBCTRL_IRQn          = 5,
  XIP_IRQn              = 6,
  PIO0_IRQ_0_IRQn       = 7,
  PIO0_IRQ_1_IRQn       = 8,
  PIO1_IRQ_0_IRQn       = 9,
  PIO1_IRQ_1_IRQn       = 10,
  DMA_IRQ_0_IRQn        = 11,
  DMA_IRQ_1_IRQn        = 12,
  IO_IRQ_BANK0_IRQn     = 13,
  IO_IRQ_QSPI_IRQn      = 14,
  SIO_IRQ_PROC0_IRQn    = 15,   /* inter-core FIFO, core 0 — the SMP IPI */
  SIO_IRQ_PROC1_IRQn    = 16,   /* inter-core FIFO, core 1 — the SMP IPI */
  CLOCKS_IRQn           = 17,
  SPI0_IRQn             = 18,
  SPI1_IRQn             = 19,
  UART0_IRQn            = 20,   /* PL011 UART0 (smp-test5: interrupt-driven TX) */
  UART1_IRQn            = 21,
  ADC_IRQ_FIFO_IRQn     = 22,
  I2C0_IRQn             = 23,
  I2C1_IRQn             = 24,
  RTC_IRQn              = 25,
  /* The RP2040 exposes 26 external IRQs (0..25). */
} IRQn_Type;

/* ---- Cortex-M0+ processor and core peripheral configuration ------------ */
#define __CM0PLUS_REV             0x0001U   /* core revision r0p1 */
#define __NVIC_PRIO_BITS          2U        /* RP2040 uses 2 priority bits */
#define __Vendor_SysTickConfig    0U        /* use the CMSIS SysTick_Config */
#define __VTOR_PRESENT            1U
#define __MPU_PRESENT             1U

#include "core_cm0plus.h"

/* ---- System clock (system_rp2040.c) ------------------------------------ */
extern uint32_t SystemCoreClock;

extern void
SystemInit (void);

extern void
SystemCoreClockUpdate (void);

#ifdef __cplusplus
}
#endif

#endif /* PICO_CMSIS_RP2040_H_ */
