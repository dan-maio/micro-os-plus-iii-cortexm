/*
 * os-c-decls.h — port-specific C declarations for the RP2350 (Cortex-M33)
 * port of the shared micro-os-plus-iii kernel.
 *
 * This header SHADOWS the Cortex-M port's
 * `cmsis-plus/rtos/port/os-c-decls.h` (the pico2 include dir is searched
 * before the cortexm one).
 *
 * Dual-branch, like the shared kernel itself:
 *  - single-core build (smp-test1): OS_USE_SMP_SCHEDULER undefined → the
 *    stock scalar reference scheduler, identical to the Cortex-M port;
 *  - SMP build (smp-test2): OS_USE_SMP_SCHEDULER + OS_NCPU 2 are defined
 *    (in the test's Makefile, so every kernel translation unit sees them)
 *    → the per-CPU current_thread_[OS_NCPU], affinity picker and per-core
 *    idle of the shared kernel's SMP variant (the Cortex-A7 pattern).
 */

#ifndef CMSIS_PLUS_RTOS_PORT_OS_C_DECLS_H_
#define CMSIS_PLUS_RTOS_PORT_OS_C_DECLS_H_

#include <stdint.h>
#include <stdbool.h>

typedef uint64_t os_port_clock_timestamp_t;
typedef uint32_t os_port_clock_duration_t;
typedef uint64_t os_port_clock_offset_t;

typedef bool os_port_scheduler_state_t;

typedef uint32_t os_port_irq_state_t;

typedef uint32_t os_port_thread_stack_element_t;
typedef uint64_t os_port_thread_stack_allocation_element_t;

typedef struct
{
  os_port_thread_stack_element_t* stack_ptr;
} os_port_thread_context_t;

/* Cross-core reschedule is signalled over the SIO inter-core FIFO (the
 * sending core pushes a word, the receiving core's SIO_IRQ_FIFO (=25) drains
 * it and pends its own PendSV). */
#define OS_SMP_IPI_FIFO 1

#if defined(OS_USE_SMP_SCHEDULER)

#define OS_NCPU 2

/* SMP scheduler lock (recursive; owned by the SIO hardware spinlock 0).
 * The actual hardware lock word lives in SIO_SPINLOCK0 (0xD0000100); here we
 * only track the recursive owner/depth. */
typedef struct
{
  volatile uint32_t owner;
  volatile uint32_t depth;
} os_port_smp_klock_t;

#define SMP_NO_OWNER 0xFFFFFFFFu

#endif /* defined(OS_USE_SMP_SCHEDULER) */

#define OS_INTEGER_RTOS_STACK_FILL_MAGIC (0xEFBEADDE)

#define OS_HAS_INTERRUPTS_STACK

#endif /* CMSIS_PLUS_RTOS_PORT_OS_C_DECLS_H_ */
