/*
 * os-app-config.h — µOS++ configuration for the RP2350 (Cortex-M33) port.
 *
 * Phase 2: single-core RTOS bring-up on core 0. The shared kernel uses its
 * reference scheduler on top of the upstream Cortex-M PendSV port + core
 * SysTick tick — OS_USE_SMP_SCHEDULER is deliberately NOT defined here (that
 * is added in Phase 3 for the 2-core SMP variant). See ../PLAN.md.
 */

#ifndef CMSIS_PLUS_OS_APP_CONFIG_H_
#define CMSIS_PLUS_OS_APP_CONFIG_H_

// SysTick RTOS tick: 1000 Hz -> 1 tick == 1 ms (sysclock.sleep_for is ms).
#define OS_INTEGER_SYSTICK_FREQUENCY_HZ (1000)

// NVIC critical-section masking priority (1..15 for __NVIC_PRIO_BITS == 4).
#define OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY (5)

// Stacks (the M33 port asserts each thread stack holds two exception frames).
#define OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES (8192)
#define OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES (4096)
// Default stack for app threads (worker/LED); µOS++ trace::printf is heavy.
#define OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES (4096)

// NOTE: OS_TRACE_RTOS_THREAD_CONTEXT (and the other OS_TRACE_RTOS_* flags) must
// stay OFF: they insert trace::printf() calls inside switch_stacks()/reschedule()
// which run in PendSV handler mode, where µOS++ trace is NOT ISR-safe and faults.

#endif /* CMSIS_PLUS_OS_APP_CONFIG_H_ */
