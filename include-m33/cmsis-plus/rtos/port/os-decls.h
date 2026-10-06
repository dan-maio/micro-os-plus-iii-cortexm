/*
 * os-decls.h — port-specific declarations for the GENERIC Cortex-M33
 * (SSE-200) port of the shared micro-os-plus-iii kernel.
 *
 * Shadows the Cortex-M port's `cmsis-plus/rtos/port/os-decls.h`. Everything
 * shared with the single-core Cortex-M build (stack word types, interrupts
 * state, scheduler state) is kept identical; the SMP additions (per-CPU
 * scheduler lock state + the kernel spinlock) are gated on
 * OS_USE_SMP_SCHEDULER.
 */

#ifndef CMSIS_PLUS_RTOS_PORT_OS_DECLS_H_
#define CMSIS_PLUS_RTOS_PORT_OS_DECLS_H_

// ----------------------------------------------------------------------------

#if defined(OS_USE_OS_APP_CONFIG_H)
#include <cmsis-plus/os-app-config.h>
#endif

#include <cmsis-plus/rtos/port/os-c-decls.h>

#if !defined(OS_INTEGER_SYSTICK_FREQUENCY_HZ)
#define OS_INTEGER_SYSTICK_FREQUENCY_HZ (1000)
#endif

#if !defined(OS_INTEGER_RTOS_MIN_STACK_SIZE_BYTES)
#define OS_INTEGER_RTOS_MIN_STACK_SIZE_BYTES (256)
#endif

#if !defined(OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES)
#define OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES (2048)
#endif

#if !defined(OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES)
#define OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES \
  (OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES)
#endif

#if !defined(OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES)
#define OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES \
  (OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES)
#endif

// ----------------------------------------------------------------------------

#include <signal.h>
// Platform definitions
#include <sys/time.h>

// ----------------------------------------------------------------------------

#ifdef __cplusplus

#include <cstdint>
#include <cstddef>

namespace os
{
  namespace rtos
  {
    namespace port
    {
      // ----------------------------------------------------------------------

      namespace stack
      {
        // Stack word.
        using element_t = os_port_thread_stack_element_t;

        // Align stack to 8 bytes.
        using allocation_element_t = os_port_thread_stack_allocation_element_t;

        // Initial value for the minimum stack size in bytes.
        constexpr std::size_t min_size_bytes
            = OS_INTEGER_RTOS_MIN_STACK_SIZE_BYTES;

        // Initial value for the default stack size in bytes.
        constexpr std::size_t default_size_bytes
            = OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES;

        // Used to fill in the stack.
        constexpr element_t magic
            = OS_INTEGER_RTOS_STACK_FILL_MAGIC; // DEADBEEF

      } /* namespace stack */

      namespace interrupts
      {
        // Type to store the entire processor interrupts mask.
        using state_t = os_port_irq_state_t;

        namespace state
        {
          constexpr state_t init = 0;
        } /* namespace state */

      } /* namespace interrupts */

      namespace scheduler
      {
        using state_t = os_port_scheduler_state_t;

        namespace state
        {
          constexpr state_t locked = true;
          constexpr state_t unlocked = false;
          constexpr state_t init = unlocked;
        } /* namespace state */

#if defined(OS_USE_SMP_SCHEDULER)

        // One scheduler lock state per core.
        extern state_t lock_state[OS_NCPU];

        // PRIMASK saved when each core took the scheduler lock, so the
        // matching unlock restores the exact interrupt state.
        extern uint32_t lock_primask[OS_NCPU];

        // SMP kernel lock (recursive; backed by an LDREX/STREX word).
        struct smp_klock_t
        {
          volatile uint32_t lock;
          volatile uint32_t owner;
          volatile uint32_t depth;
        };

        extern smp_klock_t _smp_klock;

#else

        extern state_t lock_state;

#endif /* defined(OS_USE_SMP_SCHEDULER) */

      } /* namespace scheduler */

      using thread_context_t = struct context_s
      {
        // On Cortex-M cores the context itself is stored on the stack,
        // only the stack pointer needs to be preserved.
        stack::element_t* stack_ptr;
      };

      // ----------------------------------------------------------------------

    } /* namespace port */
  } /* namespace rtos */
} /* namespace os */

// ----------------------------------------------------------------------------

#endif /* __cplusplus */

#endif /* CMSIS_PLUS_RTOS_PORT_OS_DECLS_H_ */
