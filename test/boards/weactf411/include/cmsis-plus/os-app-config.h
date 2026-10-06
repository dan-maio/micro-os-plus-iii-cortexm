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

// A harness suite built as a board app (test/weactf411/tests.cmake,
// weactf411-harness-suite) carries its own os-app-config.h, and on every
// harness platform that file IS the configuration. This board's include
// directory is searched first, so it is this file the kernel finds: hand over
// to the suite's, and let the values below fill only what the suite leaves
// unset. A board test defines no UOS_HARNESS_SUITE and gets exactly the
// values below.
#if defined(UOS_HARNESS_SUITE)
#include_next <cmsis-plus/os-app-config.h>

// rtos-apis asks for a separate 512 KiB RTOS system arena, carved from the
// application free store at startup; the STM32F411CE has 128 KiB of SRAM in
// all. Cap the arena so it fits (its pools are three objects each). The macro
// must stay defined: the system out-of-memory hook exists only under it.
#if defined(OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES)
#undef OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES
#define OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES (32 * 1024)
#endif

// rtos-apis's chan-fatfs sub-test builds RAM disks of up to ~33 MiB; they
// cannot fit 128 KiB of SRAM. Skip it, as the pico2 does on hardware.
#define OS_EXCLUDE_RTOS_APIS_FATFS
#endif /* defined(UOS_HARNESS_SUITE) */

// ---- SysTick RTOS tick ----------------------------------------------------

// 1000 Hz -> 1  tick == 1 ms, so sysclock.sleep_for(n) sleeps n milliseconds.
// The port programs SysTick via SysTick_Config(SystemCoreClock / freq).
#ifndef OS_INTEGER_SYSTICK_FREQUENCY_HZ
#define OS_INTEGER_SYSTICK_FREQUENCY_HZ (1000)
#endif

// ---- NVIC critical sections ----------------------------------------------

// Interrupts with priority numerically >= this value are masked (BASEPRI)
// inside RTOS critical sections; higher-priority (lower-number) IRQs stay
// live. Must be in 1..15 for __NVIC_PRIO_BITS == 4. SysTick/PendSV are set
// to the lowest priority (15) by the port so they never pre-empt a section.
//
// Not a fallback for a suite: whether a suite masks with BASEPRI or with
// PRIMASK is the suite's choice (cmsis-os-validator leaves it undefined on
// purpose -- its ISRs call the RTOS at any priority).
#if !defined(UOS_HARNESS_SUITE)
#define OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY (5)
#endif

// ---- Stacks ---------------------------------------------------------------

// Note: the Cortex-M port's context::create() asserts the stack holds room for
// two exception frames, comparing size-in-elements against 2*sizeof(frame_t)
// in bytes, so every thread stack must exceed ~544 bytes. Keep these >= 1 KiB.
#ifndef OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES
#define OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES (2048)
#endif
#ifndef OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES
#define OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES (1024)
#endif

// ---- Diagnostics / trace --------------------------------------------------

// Route trace::printf() to the host console through ARM semihosting
// (BKPT 0xAB / SYS_WRITE0) via the debug probe, as the shared-loaders
// reference does.
#ifndef OS_USE_TRACE_SEMIHOSTING_DEBUG
#define OS_USE_TRACE_SEMIHOSTING_DEBUG (1)
#endif

#endif /* CMSIS_PLUS_OS_APP_CONFIG_H_ */
