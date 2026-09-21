/*
 * os-app-config.h — application configuration for the WeAct Studio F411CE
 * (STM32F411CE, Cortex-M4F) single-CPU build of micro-os-plus-iii.
 *
 * This is a bare Cortex-M4 target: the RTOS tick is driven by the core
 * SysTick, context switches run in PendSV, and interrupts are managed by
 * the NVIC (__NVIC_PRIO_BITS == 4 on STM32F4 -> priorities 0..15).
 *
 * Scheduler model: the shared micro-os-plus-iii *reference* scheduler runs on
 * top of the upstream Cortex-M port's PendSV context switch (the port supplies
 * port::scheduler / port::context; the kernel supplies the ready lists). This
 * is the stock, single-CPU µOS++ Cortex-M configuration -- built against
 * cortexm/'s kernel copy, patched like every copy; OS_USE_SMP_SCHEDULER is
 * not defined here, so the patch's SMP branch compiles out.
 *
 * Board bring-up: the BSP early hook switches the core from HSI to the
 * HSE 25 MHz crystal -> PLL -> 100 MHz configuration (flash 3 wait states),
 * so SystemCoreClock == 100 MHz when the scheduler starts and the SysTick
 * interval and the USART1 (APB2) baud divisor are computed from it.
 */

#ifndef CMSIS_PLUS_OS_APP_CONFIG_H_
#define CMSIS_PLUS_OS_APP_CONFIG_H_

// ---- SysTick RTOS tick ----------------------------------------------------

// 1000 Hz -> 1  tick == 1 ms, so sysclock.sleep_for(n) sleeps n milliseconds.
// The port programs SysTick via SysTick_Config(SystemCoreClock / freq).
#define OS_INTEGER_SYSTICK_FREQUENCY_HZ (1000)

// ---- NVIC critical sections ----------------------------------------------

// Interrupts with priority numerically >= this value are masked (BASEPRI)
// inside RTOS critical sections; higher-priority (lower-number) IRQs stay
// live. Must be in 1..15 for __NVIC_PRIO_BITS == 4. SysTick/PendSV are set
// to the lowest priority (15) by the port so they never pre-empt a section.
#define OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY (5)

// ---- Stacks ---------------------------------------------------------------

// Note: the Cortex-M port's context::create() asserts the stack holds room for
// two exception frames, comparing size-in-elements against 2*sizeof(frame_t)
// in bytes, so every thread stack must exceed ~544 bytes. Keep these >= 1 KiB.
#define OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES (2048)
#define OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES (1024)

// ---- Diagnostics / trace --------------------------------------------------

// Route trace::printf() to the host console through ARM semihosting
// (BKPT 0xAB / SYS_WRITE0) via the debug probe, as the shared-loaders
// reference does.
#define OS_USE_TRACE_SEMIHOSTING_DEBUG (1)

#endif /* CMSIS_PLUS_OS_APP_CONFIG_H_ */
