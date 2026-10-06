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

// A harness suite built as a board app (test/pico2/tests.cmake,
// pico2-harness-suite) carries its own os-app-config.h, and on every harness
// platform that file IS the configuration -- rtos-apis needs its thread
// statistics and its 512 KiB of RTOS memory. This board's include directory is
// searched first, so it is this file the kernel finds: hand over to the
// suite's, and let the values below fill only what the suite leaves unset.
// A board test defines no UOS_HARNESS_SUITE and gets exactly the values below.
#if defined(UOS_HARNESS_SUITE)
#include_next <cmsis-plus/os-app-config.h>

// rtos-apis's own config asks for a separate RTOS system arena
// (OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES = 512 KiB), carved from the
// application free store at startup. pico2's free store is only 128 KiB, so
// that allocation throws bad_alloc and the run aborts before it starts. On
// hardware, cap the arena so it fits (96 KiB still covers FatFs's 32 KiB
// working buffer plus the RTOS objects). The macro must stay defined: the
// system out-of-memory hook exists only under it. QEMU has a large free store
// and keeps the suite's value, so this is gated on the hardware build.
#if defined(HW_BUILD) && defined(OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES)
#undef OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES
#define OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES (96 * 1024)
#endif

// rtos-apis's chan-fatfs sub-test builds RAM disks up to 66130 blocks * 512 B
// (~33 MiB); they cannot fit the RP2350's 520 KiB SRAM. Skip it on hardware.
#if defined(HW_BUILD)
#define OS_EXCLUDE_RTOS_APIS_FATFS
#endif
#endif

// SysTick RTOS tick: 1000 Hz -> 1 tick == 1 ms (sysclock.sleep_for is ms).
#ifndef OS_INTEGER_SYSTICK_FREQUENCY_HZ
#define OS_INTEGER_SYSTICK_FREQUENCY_HZ (1000)
#endif

// NVIC critical-section masking priority (1..15 for __NVIC_PRIO_BITS == 4).
#ifndef OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY
#define OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY (5)
#endif

// Stacks (the M33 port asserts each thread stack holds two exception frames).
#ifndef OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES
#define OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES (8192)
#endif
#ifndef OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES
#define OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES (4096)
#endif
// Default stack for app threads (worker/LED); µOS++ trace::printf is heavy.
#ifndef OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES
#define OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES (4096)
#endif

// NOTE: OS_TRACE_RTOS_THREAD_CONTEXT (and the other OS_TRACE_RTOS_* flags) must
// stay OFF: they insert trace::printf() calls inside switch_stacks()/reschedule()
// which run in PendSV handler mode, where µOS++ trace is NOT ISR-safe and faults.

#endif /* CMSIS_PLUS_OS_APP_CONFIG_H_ */
