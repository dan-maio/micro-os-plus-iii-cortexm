/*
 * os-inlines.h — inline port implementations for the GENERIC Cortex-M33
 * (SSE-200) port of the shared micro-os-plus-iii kernel.
 *
 * Shadows the Cortex-M port's `cmsis-plus/rtos/port/os-inlines.h`.
 *
 * Dual-branch, gated on OS_USE_SMP_SCHEDULER:
 *  - single-core: identical to the Cortex-M port (DI/EI or BASEPRI critical
 *    sections, scalar lock_state);
 *  - SMP: per-core lock_state, the recursive kernel spinlock `_smp_klock`
 *    backed by an LDREX/STREX word, and PRIMASK-masked critical sections.
 *
 * On QEMU's mps2-an521 the SSE-200 CPUID register (0x4001F000) returns the
 * accessing core's index; the cross-core IPI is the SSE-200 MHU (see
 * os-core-m33.cpp).
 */

#ifndef CMSIS_PLUS_RTOS_PORT_OS_INLINES_H_
#define CMSIS_PLUS_RTOS_PORT_OS_INLINES_H_

// ----------------------------------------------------------------------------

#if defined(OS_USE_OS_APP_CONFIG_H)
#include <cmsis-plus/os-app-config.h>
#endif

#include <cmsis-plus/rtos/os-c-decls.h>

// ----------------------------------------------------------------------------

#ifdef __cplusplus

#include <cmsis_device.h>

#include <cmsis-plus/diag/trace.h>

namespace os
{
  namespace rtos
  {
    namespace port
    {
      // The SSE-200 CPU_IDENTITY register block; every core sees its own
      // container at this address and reads its own index from CPUID.
      constexpr uintptr_t sse200_cpuid_base = 0x4001F000u;

      // ----------------------------------------------------------------------

      namespace scheduler
      {

        inline void __attribute__ ((always_inline))
        greeting (void)
        {
#if defined(OS_USE_SMP_SCHEDULER)
          trace::printf (
              "Scheduler: µOS++ Cortex-M33 SMP (SSE-200), %d cores, 1ms tick\n",
              OS_NCPU);
#else
          trace::printf ("Scheduler: µOS++ Cortex-M33 (SSE-200)");
#if defined(__ARM_FP)
          trace::printf (" FP");
#endif
          trace::printf (", preemptive");
#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY)
          trace::printf (", BASEPRI(%u)",
                         OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY);
#else
          trace::printf (", DI/EI");
#endif
#if defined(OS_EXCLUDE_RTOS_IDLE_SLEEP)
          trace::printf (", no WFI");
#else
          trace::printf (", WFI");
#endif
          trace::puts ("");
          trace::printf ("System clock: %u Hz\n", SystemCoreClock);
#endif /* defined(OS_USE_SMP_SCHEDULER) */
        }

        inline port::scheduler::state_t __attribute__ ((always_inline))
        lock (void)
        {
          return locked (state::locked);
        }

        inline port::scheduler::state_t __attribute__ ((always_inline))
        unlock (void)
        {
          return locked (state::unlocked);
        }

#if defined(OS_USE_SMP_SCHEDULER)

        // Current core index, read from the SSE-200 CPUID register.
        inline unsigned __attribute__ ((always_inline))
        port_cpu_id (void)
        {
          return *reinterpret_cast<volatile uint32_t*> (sse200_cpuid_base)
                 & 0xFu;
        }

        inline bool __attribute__ ((always_inline))
        locked (void)
        {
          return lock_state[port_cpu_id ()] != state::unlocked;
        }

#else

        inline bool __attribute__ ((always_inline))
        locked (void)
        {
          return lock_state != state::unlocked;
        }

#endif /* defined(OS_USE_SMP_SCHEDULER) */

        inline void __attribute__ ((always_inline))
        wait_for_interrupt (void)
        {
#if !defined(OS_EXCLUDE_RTOS_IDLE_SLEEP)
#if defined(OS_TRACE_RTOS_THREAD_CONTEXT)
          trace::printf ("%s() \n", __func__);
#endif
          // The DSB is recommended by ARM before WFI.
          __DSB ();
          __WFI ();
#endif /* !defined(OS_EXCLUDE_RTOS_IDLE_SLEEP) */
        }

#if defined(OS_USE_SMP_SCHEDULER)

        // ---- SMP kernel lock (LDREX/STREX word) -----------------------------

        // Acquire the lock word. The exclusive monitor is only a hint; the
        // final STREX is the atomic step (a failed STREX retries).
        inline void __attribute__ ((always_inline))
        _smp_klock_raw_acquire (void)
        {
          uint32_t tmp, fail;
          __asm__ volatile (
              "1:  ldrex   %0, [%2]      \n"
              "    cmp     %0, #0        \n"
              "    bne     1b            \n"
              "    mov     %0, #1        \n"
              "    strex   %1, %0, [%2]  \n"
              "    cmp     %1, #0        \n"
              "    bne     1b            \n"
              "    dmb                   \n"
              : "=&r"(tmp), "=&r"(fail) /* out */
              : "r"(&scheduler::_smp_klock.lock) /* in */
              : "cc", "memory");
        }

        inline void __attribute__ ((always_inline))
        _smp_klock_raw_release (void)
        {
          __DMB ();
          scheduler::_smp_klock.lock = 0;
          __DSB ();
        }

        // Mask IRQs on this core and enter the kernel lock (recursive, with
        // owner tracking so nested critical sections only bump the depth).
        // Return the previous PRIMASK so the matching port_put_lock() can
        // restore it; passing a hardcoded 0 would wrongly re-enable IRQs when
        // the lock is taken with interrupts already disabled (e.g. during the
        // port scheduler start()).
        inline uint32_t __attribute__ ((always_inline))
        port_set_lock (void)
        {
          uint32_t pri = __get_PRIMASK ();
          __asm__ volatile ("cpsid i" ::: "memory");
          unsigned cpu = port_cpu_id ();
          if (scheduler::_smp_klock.owner != cpu)
            {
              scheduler::_smp_klock_raw_acquire ();
              scheduler::_smp_klock.owner = cpu;
            }
          scheduler::_smp_klock.depth = scheduler::_smp_klock.depth + 1;
          return pri;
        }

        // Leave the kernel lock and restore the given PRIMASK state.
        inline void __attribute__ ((always_inline))
        port_put_lock (uint32_t pri)
        {
          unsigned cpu = port_cpu_id ();
          if (scheduler::_smp_klock.owner == cpu
              && scheduler::_smp_klock.depth > 0)
            {
              uint32_t d = scheduler::_smp_klock.depth - 1;
              scheduler::_smp_klock.depth = d;
              if (d == 0)
                {
                  scheduler::_smp_klock.owner = SMP_NO_OWNER;
                  scheduler::_smp_klock_raw_release ();
                }
            }
          __set_PRIMASK (pri);
        }

#endif /* defined(OS_USE_SMP_SCHEDULER) */

      } /* namespace scheduler */

      // The DSB/ISB are recommended by ARM after priority level changes.

      namespace interrupts
      {
        inline bool __attribute__ ((always_inline))
        in_handler_mode (void)
        {
          return (__get_IPSR () != 0);
        }

        inline bool
        is_priority_valid (void)
        {
#if defined(OS_USE_SMP_SCHEDULER)
          // With DI/EI (PRIMASK) critical sections, all priorities are valid.
          return true;
#else
#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY)

          uint32_t exception_number = __get_IPSR ();
          if (exception_number > 0)
            {
              uint32_t prio = NVIC_GetPriority (
                  static_cast<IRQn_Type> (exception_number - 16));
              return (prio
                      >= OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY);
            }
          else
            {
              return true;
            }

#else

          // When using DI/EI, all priority levels are valid.
          return true;

#endif /* defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) */
#endif /* defined(OS_USE_SMP_SCHEDULER) */
        }

#if defined(OS_USE_SMP_SCHEDULER)

        inline rtos::interrupts::state_t __attribute__ ((always_inline))
        critical_section::enter (void)
        {
          uint32_t pri = __get_PRIMASK ();
          scheduler::port_set_lock ();
          return pri;
        }

        inline void __attribute__ ((always_inline))
        critical_section::exit (rtos::interrupts::state_t state)
        {
          scheduler::port_put_lock (state);
        }

#else

        inline rtos::interrupts::state_t __attribute__ ((always_inline))
        critical_section::enter (void)
        {
          uint32_t pri;

#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY)

          pri = __get_BASEPRI ();
          __set_BASEPRI_MAX (
              OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY
              << ((8 - __NVIC_PRIO_BITS)));

#else

          pri = __get_PRIMASK ();
          __disable_irq ();

#endif /* defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) */

          return pri;
        }

        inline void __attribute__ ((always_inline))
        critical_section::exit (rtos::interrupts::state_t state)
        {
#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY)

          __set_BASEPRI (state);

#else

          __set_PRIMASK (state);

#endif /* defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) */
        }

#endif /* defined(OS_USE_SMP_SCHEDULER) */

        // ====================================================================

        inline rtos::interrupts::state_t __attribute__ ((always_inline))
        uncritical_section::enter (void)
        {
          uint32_t pri;

#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) \
    && !defined(OS_USE_SMP_SCHEDULER)

          pri = __get_BASEPRI ();
          __set_BASEPRI (0);

#else

          pri = __get_PRIMASK ();
          __enable_irq ();

#endif

          return pri;
        }

        inline void __attribute__ ((always_inline))
        uncritical_section::exit (rtos::interrupts::state_t state)
        {
#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) \
    && !defined(OS_USE_SMP_SCHEDULER)

          __set_BASEPRI (state);

#else

          __set_PRIMASK (state);

#endif
        }

      } /* namespace interrupts */

      // ======================================================================

      namespace this_thread
      {
        inline void __attribute__ ((always_inline))
        prepare_suspend (void)
        {
          ;
        }

      } /* namespace this_thread */

      // ======================================================================

      inline void __attribute__ ((always_inline))
      clock_highres::start (void)
      {
        ;
      }

      inline constexpr bool __attribute__ ((always_inline))
      clock_highres::has_hardware_counter (void) noexcept
      {
        return false;
      }

      inline uint64_t __attribute__ ((always_inline))
      clock_highres::hardware_counter (void) noexcept
      {
        return 0;
      }

      inline uint32_t __attribute__ ((always_inline))
      clock_highres::input_clock_frequency_hz (void)
      {
        return SystemCoreClock;
      }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
      inline uint32_t __attribute__ ((always_inline))
      clock_highres::cycles_per_tick (void)
      {
        return SysTick->LOAD + 1;
      }

      inline uint32_t __attribute__ ((always_inline))
      clock_highres::cycles_since_tick (void)
      {
        uint32_t load_value = SysTick->LOAD;
        uint32_t val = SysTick->VAL;

        if (SysTick->CTRL & SCB_ICSR_PENDSTSET_Msk)
          {
            uint32_t val_subsequent = SysTick->VAL;
            if (val > val_subsequent)
              {
                return load_value + 1 + (load_value - val);
              }
          }

        return load_value - val;
      }
#pragma GCC diagnostic pop

      // ======================================================================

    } /* namespace port */
  } /* namespace rtos */
} /* namespace os */

#pragma GCC diagnostic pop

// ----------------------------------------------------------------------------

#endif /* __cplusplus */

#endif /* CMSIS_PLUS_RTOS_PORT_OS_INLINES_H_ */
