/*
 * os-c-decls.h — port-specific C declarations for the GENERIC Cortex-M33
 * port of the shared micro-os-plus-iii kernel.
 *
 * This is the QEMU analogue of include-rp2350/: same ISA and same dual-branch
 * scheduler, but the machine primitives are the SSE-200's (CPUID + Message
 * Handling Unit) instead of the RP2350's SIO. It is used by the `cortexm-qemu-m33`
 * library, i.e. by the `pico2-1cpu` (single core) and `2xcortex-m33` (SMP)
 * configurations.
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

/* Cross-core reschedule is signalled over the SSE-200 MHU (the sending core
 * writes the target's INTR_SET register, the receiving core takes its MHU IRQ
 * and pends its own PendSV). */
#define OS_SMP_IPI_MHU 1

#if defined(OS_USE_SMP_SCHEDULER)

/* SMP scheduler lock: recursive, owned by a cross-core spinlock word. The
 * word is acquired/released with LDREX/STREX in os-inlines.h; owner/depth make
 * it recursive so nested critical sections are cheap. */
typedef struct
{
  volatile uint32_t lock;
  volatile uint32_t owner;
  volatile uint32_t depth;
} os_port_smp_klock_t;

#define SMP_NO_OWNER 0xFFFFFFFFu

#endif /* defined(OS_USE_SMP_SCHEDULER) */

#define OS_INTEGER_RTOS_STACK_FILL_MAGIC (0xEFBEADDE)

#define OS_HAS_INTERRUPTS_STACK

#endif /* CMSIS_PLUS_RTOS_PORT_OS_C_DECLS_H_ */
