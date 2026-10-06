/*
 * os-inlines.h — inline port implementations for the RP2350 (Cortex-M33)
 * port of the shared micro-os-plus-iii kernel.
 *
 * Shadows the Cortex-M port's `cmsis-plus/rtos/port/os-inlines.h`.
 *
 * Dual-branch, gated on OS_USE_SMP_SCHEDULER:
 *  - single-core: identical to the Cortex-M port (DI/EI or BASEPRI critical
 *    sections, scalar lock_state) — the board-verified Phase 2 behaviour;
 *  - SMP: per-core lock_state, the recursive kernel spinlock `_smp_klock`
 *    backed by the RP2350 SIO hardware spinlock 0, and PRIMASK-masked
 *    critical sections (see the SMP model notes below).
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

#if defined(OS_USE_SMP_SCHEDULER)
#include <bsp/rp2350.hpp>
#endif /* defined(OS_USE_SMP_SCHEDULER) */

namespace os
{
  namespace rtos
  {
    namespace port
    {
      // ----------------------------------------------------------------------

      namespace scheduler
      {

        inline void __attribute__ ((always_inline))
        greeting (void)
        {
#if defined(OS_USE_SMP_SCHEDULER)
          trace::printf (
              "Scheduler: µOS++ RP2350 Cortex-M33 SMP, 2 cores, 1ms tick\n");
          // At this stage the system clock should have already been configured
          // at 150 MHz by the pico2_early_init constructor.
          trace::printf ("System clock: %u Hz\n", SystemCoreClock);
#else
          trace::printf ("Scheduler: µOS++ Cortex-M");
#if defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_MAIN__)
          trace::putchar ('0' + __CORTEX_M); // M4/M7
#if defined(__ARM_FP)
          trace::printf (" FP");
#endif
#elif defined(__ARM_ARCH_7M__)
          trace::printf ("3");
#elif defined(__ARM_ARCH_6M__)
          trace::printf ("0/0+");
#endif
          trace::printf (", preemptive");
#if (defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) \
     || defined(__ARM_ARCH_8M_MAIN__)) \
    && defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY)
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

        // Current core index, read from SIO_CPUID (0xD0000000).
        inline unsigned __attribute__ ((always_inline))
        port_cpu_id (void)
        {
          return rp2350::cpu_id ();
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

        // ---- SMP kernel lock (SIO hardware spinlock 0) ----------------------

        // Claim the hardware spinlock (reading it returns 0 while another
        // core holds it; any write releases it).
        inline void __attribute__ ((always_inline))
        _smp_klock_raw_acquire (void)
        {
          while (rp2350::reg (rp2350::SIO_SPINLOCK0) == 0)
            {
            }
          __asm__ volatile ("dmb" ::: "memory");
        }

        inline void __attribute__ ((always_inline))
        _smp_klock_raw_release (void)
        {
          __asm__ volatile ("dmb" ::: "memory");
          rp2350::reg (rp2350::SIO_SPINLOCK0) = 1;
          __asm__ volatile ("dsb" ::: "memory");
        }

        // Mask IRQs on this core and enter the kernel lock (recursive, with
        // owner tracking so nested critical sections only bump the depth).
        inline void __attribute__ ((always_inline))
        port_set_lock (void)
        {
          __asm__ volatile ("cpsid i" ::: "memory");
          unsigned cpu = port_cpu_id ();
          if (_smp_klock.owner != cpu)
            {
              _smp_klock_raw_acquire ();
              _smp_klock.owner = cpu;
            }
          _smp_klock.depth = _smp_klock.depth + 1;
        }

        // Leave the kernel lock and restore the given PRIMASK state.
        inline void __attribute__ ((always_inline))
        port_put_lock (uint32_t pri)
        {
          unsigned cpu = port_cpu_id ();
          if (_smp_klock.owner == cpu && _smp_klock.depth > 0)
            {
              uint32_t d = _smp_klock.depth - 1;
              _smp_klock.depth = d;
              if (d == 0)
                {
                  _smp_klock.owner = SMP_NO_OWNER;
                  _smp_klock_raw_release ();
                }
            }
          __set_PRIMASK (pri);
        }

#endif /* defined(OS_USE_SMP_SCHEDULER) */

      } /* namespace scheduler */

      // The DSB/ISB are recommended by ARM after priority level changes.
      // (Application Note 321: ARM Cortex-M Programming Guide to
      // Memory Barrier Instructions)
      // http://infocenter.arm.com/help/topic/com.arm.doc.dai0321a/DAI0321A_programming_guide_memory_barriers_for_m_profile.pdf

      namespace interrupts
      {
        inline bool __attribute__ ((always_inline))
        in_handler_mode (void)
        {
          // In Handler mode, IPSR holds the exception number.
          // If 0, the core is in thread mode.
          return (__get_IPSR () != 0);
        }

        inline bool
        is_priority_valid (void)
        {
#if defined(OS_USE_SMP_SCHEDULER)
          // With DI/EI (PRIMASK) critical sections, all priorities are valid.
          return true;
#else
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) \
    || defined(__ARM_ARCH_8M_MAIN__)

#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY)

          // The trick here is to properly account for differences
          // between Cortex-M exception number stored in IPSR (an
          // unsigned byte), and the CMSIS interrupt number (IRQn).
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

#elif defined(__ARM_ARCH_6M__)

          // When using DI/EI, all priority levels are valid.
          return true;

#endif /* architecture */
#endif /* defined(OS_USE_SMP_SCHEDULER) */
        }

#if defined(OS_USE_SMP_SCHEDULER)

        // Enter an IRQ critical section: mask this core's IRQs and take the
        // kernel spinlock so the shared data is also safe against the other
        // core. Recursive (owner+depth), so nested sections are fine.
        inline rtos::interrupts::state_t __attribute__ ((always_inline))
        critical_section::enter (void)
        {
          uint32_t pri = __get_PRIMASK ();
          scheduler::port_set_lock ();
          return pri;
        }

        // Exit an IRQ critical section.
        inline void __attribute__ ((always_inline))
        critical_section::exit (rtos::interrupts::state_t state)
        {
          scheduler::port_put_lock (state);
        }

#else

        // Enter an IRQ critical section (single-core, like the Cortex-M port).
        inline rtos::interrupts::state_t __attribute__ ((always_inline))
        critical_section::enter (void)
        {
          uint32_t pri;

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) \
    || defined(__ARM_ARCH_8M_MAIN__)

#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY)

          // Read the current BASEPRI, to be returned and later restored.
          pri = __get_BASEPRI ();

          // According to ARM "MSR{cond} spec_reg, Rn":
          // When you write to BASEPRI_MAX, the instruction writes to
          // BASEPRI only if either:
          // Rn is non-zero and the current BASEPRI value is 0
          // Rn is non-zero and less than the current BASEPRI value.
          __set_BASEPRI_MAX (
              OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY
              << ((8 - __NVIC_PRIO_BITS)));

#else

          // Read the current PRIMASK, to be returned and later restored.
          pri = __get_PRIMASK ();

          // Disable all interrupts.
          __disable_irq ();

#endif /* defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) */

#elif defined(__ARM_ARCH_6M__)

          // Read the current PRIMASK, to be returned and later restored.
          pri = __get_PRIMASK ();

          // Disable all interrupts.
          __disable_irq ();

#endif

          return pri;
        }

        // Exit an IRQ critical section (single-core).
        inline void __attribute__ ((always_inline))
        critical_section::exit (rtos::interrupts::state_t state)
        {
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) \
    || defined(__ARM_ARCH_8M_MAIN__)

#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY)

          // Restore BASEPRI to the value saved by enter().
          __set_BASEPRI (state);

#else

          // Restore PRIMASK to the value saved by enter().
          __set_PRIMASK (state);

#endif /* defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) */

#elif defined(__ARM_ARCH_6M__)

          // Restore PRIMASK to the value saved by enter().
          __set_PRIMASK (state);

#endif
        }

#endif /* defined(OS_USE_SMP_SCHEDULER) */

        // ====================================================================

        // Enter an IRQ uncritical section.
        inline rtos::interrupts::state_t __attribute__ ((always_inline))
        uncritical_section::enter (void)
        {
          uint32_t pri;

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) \
    || defined(__ARM_ARCH_8M_MAIN__)

#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) \
    && !defined(OS_USE_SMP_SCHEDULER)

          pri = __get_BASEPRI ();

          // Setting BASEPRI to 0 makes it ineffective,
          // practically enabling all interrupts.
          __set_BASEPRI (0);

#else

          pri = __get_PRIMASK ();

          // Enable all interrupts.
          __enable_irq ();

#endif /* defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) \
          && !defined(OS_USE_SMP_SCHEDULER) */

#elif defined(__ARM_ARCH_6M__)

          pri = __get_PRIMASK ();

          // Enable all interrupts.
          __enable_irq ();

#endif

          return pri;
        }

        // Exit an IRQ uncritical section.
        inline void __attribute__ ((always_inline))
        uncritical_section::exit (rtos::interrupts::state_t state)
        {
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) \
    || defined(__ARM_ARCH_8M_MAIN__)

#if defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) \
    && !defined(OS_USE_SMP_SCHEDULER)

          // Restore BASEPRI to the value saved by enter().
          __set_BASEPRI (state);

#else

          // Restore PRIMASK to the value saved by enter().
          __set_PRIMASK (state);

#endif /* defined(OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY) \
          && !defined(OS_USE_SMP_SCHEDULER) */

#elif defined(__ARM_ARCH_6M__)

          // Restore PRIMASK to the value saved by enter().
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
#if defined(OS_USE_SMP_SCHEDULER)
        if (rp2350::reg (rp2350::RESETS_BASE + 0x00U) & (1U << 23U))
          {
            rp2350::reg (rp2350::RESETS_BASE + 0x00U) &= ~(1U << 23U);
            while (!(rp2350::reg (rp2350::RESETS_BASE + 0x08U) & (1U << 23U)))
              {
              }
          }
        rp2350::reg (rp2350::TICKS_BASE + 0x1CU) = 12U;
        rp2350::reg (rp2350::TICKS_BASE + 0x18U) = 1U;
#endif
      }

      inline constexpr bool __attribute__ ((always_inline))
      clock_highres::has_hardware_counter (void) noexcept
      {
#if defined(OS_USE_SMP_SCHEDULER)
        return true;
#else
        return false;
#endif
      }

      inline uint64_t __attribute__ ((always_inline))
      clock_highres::hardware_counter (void) noexcept
      {
#if defined(OS_USE_SMP_SCHEDULER)
        constexpr std::uint32_t timer0_base = 0x400B0000U;
        uint32_t hi = rp2350::reg (timer0_base + 0x08U);
        uint32_t lo = rp2350::reg (timer0_base + 0x0CU);
        return (static_cast<uint64_t> (hi) << 32) | lo;
#else
        return 0;
#endif
      }

      inline uint32_t __attribute__ ((always_inline))
      clock_highres::input_clock_frequency_hz (void)
      {
#if defined(OS_USE_SMP_SCHEDULER)
        return 1000000U;
#else
        // The SysTick is clocked with the CPU clock.
        return SystemCoreClock;
#endif
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

        // Initial sample of the decrementing counter.
        uint32_t val = SysTick->VAL;

        // Check overflow. If the exception is pending, the total cycles
        // count in steady_count_ does not yet reflect the correct value.
        if ((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0)
          {
            uint32_t val_subsequent = SysTick->VAL;

            // If the value did decrease, the timer did not recycle
            // between the two reads.
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
