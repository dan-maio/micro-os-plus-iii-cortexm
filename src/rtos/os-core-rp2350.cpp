/*
 * os-core.cpp — RP2350 (Cortex-M33) port of the shared micro-os-plus-iii
 * kernel. DUAL-BRANCH, gated on OS_USE_SMP_SCHEDULER (like the port headers
 * in include/cmsis-plus/rtos/port/):
 *
 *  - single-core (smp-test1, OS_USE_SMP_SCHEDULER undefined): the stock
 *    scalar reference scheduler — scalar current_thread_/lock_state, PRIMASK
 *    critical sections, no spinlock, no core-1 launch. This replaces the
 *    upstream Cortex-M port so the pico2 build has NO dependency on the
 *    micro-os-plus-iii-cortexm submodule.
 *  - SMP (smp-test2..5): the M33 SMP port (Phase 3), the Cortex-A7 analogue
 *    adapted to the M33 exception model.
 *
 * SMP notes:
 *
 *  - Context switch: cooperative PendSV (lowest priority), one per core.
 *    The PendSV handler runs on the MSP (interrupt stack), so the outgoing
 *    thread's saved frame on its PSP is quiescent once save_on_stack()
 *    returns — the stack_ptr can be published to the outgoing thread
 *    immediately inside switch_stacks() (no A7-style deferred publish; that
 *    deferral exists on the A7 because its IRQ handler runs on the
 *    interrupted thread's own stack).
 *  - Per-core SysTick: only core 0 advances the RTOS clock
 *    (os_systick_handler()); core 1's SysTick only reschedules core 1.
 *    Cross-core wakeup therefore has <= 1 ms latency (the target core's own
 *    SysTick), exactly like the A7 (per-core private timer).
 *  - Cross-core IPI (for the interrupt tests): SIO inter-core FIFO → the
 *    receiving core's SIO_IRQ_FIFO (external IRQ 25) drains the FIFO and
 *    pends its own PendSV.
 *  - Mutual exclusion: the recursive kernel spinlock `_smp_klock` backed by
 *    the RP2350 SIO hardware spinlock 0 (see os-inlines.h).
 */

#include <cmsis-plus/rtos/os.h>
#include <cmsis-plus/diag/trace.h>

#include <cmsis_device.h>

#include <bsp/multicore.hpp>
#include <bsp/rp2350.hpp>

// Switch instrumentation, defined in the BSP; written by switch_stacks().
extern "C" volatile void* g_sw_old;
extern "C" volatile void* g_sw_new;
extern "C" volatile void* g_sw_outsp;
extern "C" const char* g_sw_old_name;
extern "C" const char* g_sw_new_name;
extern "C" volatile unsigned long g_sw_count;
extern "C" const char* g_sw_hist[8];

// Boot breadcrumb, defined in the BSP; set by scheduler::start().
extern "C" void g_boot_mark (unsigned);

extern "C"
{
  extern unsigned int _Heap_Limit;
  extern unsigned int __stack;
  extern unsigned int __StackLimit;
  void os_systick_handler (void); // kernel: src/rtos/os-clocks.cpp
}

// The kernel's real idle thread (core 0) and the per-core idle array
// (os-idle.cpp / os-core.cpp); neither is declared in a public header.
extern os::rtos::thread* os_idle_thread;
#if defined(OS_USE_SMP_SCHEDULER)
namespace os
{
  namespace rtos
  {
    namespace scheduler
    {
      extern thread* os_idle_thread_core[OS_NCPU];
    }
  }
}
#endif /* defined(OS_USE_SMP_SCHEDULER) */

namespace os
{
  namespace rtos
  {
    namespace port
    {
      namespace stack
      {
        // Stack frame, as used by PendSV (see the Cortex-M port for the full
        // layout documentation).

        typedef struct frame_s
        {
          // Restored manually by ldmia %[r]!, {r4-r9,sl,fp[,r14]}.
          stack::element_t r4;
          stack::element_t r5;
          stack::element_t r6;
          stack::element_t r7;
          stack::element_t r8;
          stack::element_t r9;
          stack::element_t r10_sl;
          stack::element_t r11_fp;
          stack::element_t r14_exec_return;
          // Restored automatically by exception return.
          stack::element_t r0;
          stack::element_t r1;
          stack::element_t r2;
          stack::element_t r3;
          stack::element_t r12;
          stack::element_t r14_lr;
          stack::element_t r15_pc;
          stack::element_t psr;
        } frame_t;
      } /* namespace stack */

      /**
       * @brief Create a new thread context on the stack.
       */
      void
      context::create (void* context, void* func, void* args)
      {
        class rtos::thread::context* th_ctx
            = static_cast<class rtos::thread::context*> (context);

        class rtos::thread::stack& stack = th_ctx->stack ();

        rtos::thread::stack::element_t* p = stack.top ();

        // Be sure the stack is large enough to hold at least
        // two exception frames.
        assert ((p - stack.bottom ())
                > static_cast<int> (2 * sizeof (stack::frame_t)));

        p -= (sizeof (stack::frame_t)
              / sizeof (rtos::thread::stack::element_t));

        // Align the frame to 8 bytes than leave one more word for the extra
        // stack element, r14_exec_return, which is the 9th.
        if ((reinterpret_cast<uintptr_t> (p) & 3) != 0)
          {
            p = reinterpret_cast<rtos::thread::stack::element_t*> (
                (reinterpret_cast<int> (p)) & (~3));
          }

        if ((reinterpret_cast<uintptr_t> (p) & 7) == 0)
          {
            --p;
          }

        stack::frame_t* f = reinterpret_cast<stack::frame_t*> (p);

        f->psr = 0x01000000; // xPSR, T bit set
        f->r15_pc = static_cast<rtos::thread::stack::element_t> (
            (reinterpret_cast<ptrdiff_t> (func)) | 1);
        f->r14_lr = static_cast<rtos::thread::stack::element_t> (
            (reinterpret_cast<ptrdiff_t> (func) + 2));
        f->r12 = 0xCCCCCCCC + 0x00010203;
        f->r3 = 0x33333333 + 0x00010203;
        f->r2 = 0x22222222 + 0x00010203;
        f->r1 = 0x11111111 + 0x00010203;
        f->r0 = reinterpret_cast<rtos::thread::stack::element_t> (args);
        f->r14_exec_return = 0xFFFFFFFD; // return to thread, PSP, 8-word frame
        f->r11_fp = 0xBBBBBBBB + 0x00010203;
        f->r10_sl = 0xAAAAAAAA + 0x00010203;
        f->r9 = 0x99999999 + 0x00010203;
        f->r8 = 0x88888888 + 0x00010203;
        f->r7 = 0x77777777 + 0x00010203;
        f->r6 = 0x66666666 + 0x00010203;
        f->r5 = 0x55555555 + 0x00010203;
        f->r4 = 0x44444444 + 0x00010203;

        th_ctx->port_.stack_ptr = p;

        assert (((reinterpret_cast<int> (&f->r0)) & 7) == 0);
      }

      void
      clock_systick::start (void)
      {
#if defined(NDEBUG)
        SysTick_Config (SystemCoreClock / rtos::clock_systick::frequency_hz);
#else
        assert (SysTick_Config (SystemCoreClock
                                / rtos::clock_systick::frequency_hz)
                == 0);
#endif
        // Set SysTick interrupt priority to the lowest level (highest value).
        NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
      }

      // ----------------------------------------------------------------------

      namespace scheduler
      {
#if defined(OS_USE_SMP_SCHEDULER)
        state_t lock_state[OS_NCPU];
        uint32_t lock_primask[OS_NCPU];
        smp_klock_t _smp_klock = { SMP_NO_OWNER, 0 };

        // Core 1's handler-mode (MSP) stack. Core 0 uses the linker main
        // stack (__StackTop); core 1 is launched by the bootrom FIFO
        // handshake with its own MSP, and its SysTick/PendSV/FIFO-IRQ frames
        // live here. 8-byte aligned: the bootrom loads this as core 1's
        // initial MSP, and AAPCS/exception handling require an 8-aligned SP.
        static stack::element_t core1_msp_stack[1024]
            __attribute__ ((aligned (8)));
#else
        state_t lock_state;
#endif /* defined(OS_USE_SMP_SCHEDULER) */

        result_t
        initialize (void)
        {
          __disable_irq ();

          return result::ok;
        }

        /**
         * @brief Start the scheduler on core 0 and pass control to the
         * first thread.
         */
        void
#if defined(__ARM_ARCH_6M__)
            __attribute__ ((optimize ("no-delete-null-pointer-checks")))
#endif
            start (void)
        {
          // Disable all interrupts, to safely change the stack.
          __disable_irq ();
          g_boot_mark (1);

          // Switch the current SP from MSP to PSP without breaking the
          // running code: initialise PSP with the same value as MSP.
          __set_PSP (__get_MSP ());
          __set_CONTROL (__get_CONTROL () | CONTROL_SPSEL_Msk);
          __ISB ();
          g_boot_mark (2);

          // Re-initialise MSP to the reset value.
          __set_MSP (*(reinterpret_cast<uint32_t*> (SCB->VTOR)));

          // Set the beginning address and size of the interrupt stack and
          // fill it with the magic word (the idle thread's
          // check_bottom_magic()/available() rely on the fill).
          // Base = __StackLimit (the .stack section base) rather than
          // _Heap_Limit: with the heap relocated to PSRAM the two are no
          // longer adjacent, and the old expression spanned unmapped space.
          rtos::interrupts::stack ()->set (
              reinterpret_cast<stack::element_t*> (&__StackLimit),
              static_cast<size_t> (&__stack - &__StackLimit)
                  * sizeof (__stack));
          {
            const std::size_t n
                = static_cast<std::size_t> (&__stack - &__StackLimit)
                  / sizeof (stack::element_t);
            stack::element_t* p
                = reinterpret_cast<stack::element_t*> (&__StackLimit);
            for (std::size_t i = 0; i < n; ++i)
              {
                p[i] = os::rtos::thread::stack::magic;
              }
          }

          // Set PendSV interrupt priority to the lowest level.
          NVIC_SetPriority (PendSV_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
          g_boot_mark (3);

          // Per-core scheduler state: a fake "current" thread so the first
          // switch has a node to relink (it is discarded immediately, never
          // restored — state_ == 0 != running, so internal_relink_running_
          // ignores it).
          static os_thread_t fake_thread;
          memset (&fake_thread, 0, sizeof (os_thread_t));
          fake_thread.name = "fake_thread";
#if defined(OS_USE_SMP_SCHEDULER)
          rtos::scheduler::current_thread_[0]
              = reinterpret_cast<rtos::thread*> (&fake_thread);

          lock_state[0] = state::init;
          lock_state[1] = state::init;
          rtos::scheduler::os_idle_thread_core[0] = ::os_idle_thread;

          // Cross-core IPI channel on this core: drain any stale FIFO data
          // left by the boot and clear the sticky WOF/ROE bits. The FIFO IRQ
          // is NOT enabled here: it must stay off while core 0 runs the
          // core-1 bootrom launch handshake (which uses the very same FIFO),
          // otherwise the handler steals the bootrom's readiness word/echoes
          // and the launch blocks forever. Core 0 enables it only after the
          // launch (port_smp_core0_ipi_enable()).
          while (rp2350::reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_VLD)
            {
              volatile uint32_t d = rp2350::reg (rp2350::SIO_FIFO_RD);
              (void) d;
            }
          rp2350::reg (rp2350::SIO_FIFO_ST) = 0x0C; // write-1-to-clear WOF|ROE
          NVIC_SetPriority (SIO_IRQ_FIFO_IRQn,
                            (1UL << __NVIC_PRIO_BITS) - 1UL);
#else
          rtos::scheduler::current_thread_
              = reinterpret_cast<rtos::thread*> (&fake_thread);

          lock_state = state::init;
#endif /* defined(OS_USE_SMP_SCHEDULER) */

          __set_BASEPRI (0);

          // Trigger the PendSV; the exception will happen after
          // re-enabling the interrupts.
          scheduler::reschedule ();
          g_boot_mark (4);

          // Enable all interrupts; allow PendSV to occur.
          __enable_irq ();
          g_boot_mark (5);

          // The context switch will occur somewhere here.
          for (;;)
            __NOP ();
        }

        // --------------------------------------------------------------------

        state_t
        locked (state_t state)
        {
          os_assert_throw (!interrupts::in_handler_mode (), EPERM);

#if defined(OS_USE_SMP_SCHEDULER)
          // Mask IRQs BEFORE reading the core id. With them enabled, a PendSV
          // between port_cpu_id() and the lock_state[] write can move this
          // thread to the other core, and it would then mark the core it left
          // -- where another thread now runs -- as locked, and leave its own
          // unmarked, so the matching unlock finds "unlocked" and never
          // releases the kernel lock. Masked, the thread cannot move.
          uint32_t pri = __get_PRIMASK ();
          __asm__ volatile ("cpsid i" ::: "memory");
          unsigned cpu = port_cpu_id ();
          state_t tmp = lock_state[cpu];

          if (state != tmp)
            {
              if (state == state::locked)
                {
                  // Take the kernel spinlock; IRQs stay masked.
                  port_set_lock ();
                  lock_primask[cpu] = pri;
                  lock_state[cpu] = state;
                }
              else
                {
                  // Publish "unlocked" BEFORE releasing the spinlock and
                  // restoring PRIMASK, as the M33 port does: otherwise an IRQ
                  // in that window sees this core still locked, skips the
                  // PendSV, and the next thread here inherits the stale flag.
                  lock_state[cpu] = state;
                  port_put_lock (lock_primask[cpu]);
                }
            }
          else
            {
              __set_PRIMASK (pri);
            }
          return tmp;
#else
          state_t tmp;

          {
            rtos::interrupts::critical_section ics;

            tmp = lock_state;
            lock_state = state;
          }

          return tmp;
#endif /* defined(OS_USE_SMP_SCHEDULER) */
        }

        // --------------------------------------------------------------------

        /**
         * @brief Request a context switch on the current core.
         *
         * @details
         * On the M33, PendSV is per-core and lowest priority, so setting
         * PendSV pending on this core defers the switch until the current
         * interrupt (if any) completes. No per-core "pending" flag is needed
         * — unlike the A7 GIC IRQ scheme, PendSV IS the deferred switch.
         */
        void
        reschedule (void)
        {
          if (rtos::scheduler::locked ()
              || (rtos::interrupts::in_handler_mode ()
                  && !rtos::scheduler::preemptive ()))
            {
              return;
            }


          // Set PendSV to request a context switch.
          SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;

          // DSB/ISB recommended by ARM after programming control registers.
          __DSB ();
          __ISB ();
        }

        // --------------------------------------------------------------------

        /**
         * @brief Switch stacks to perform the rescheduling (the heart of the
         * scheduler; called from PendSV_Handler, on the MSP).
         *
         * @details
         * The whole switch runs while holding the kernel spinlock, so no
         * other core can touch the ready lists concurrently. The outgoing
         * thread's saved frame is on its own PSP, which is quiescent once
         * save_on_stack() returned — this function runs on the MSP — so the
         * stack_ptr is published immediately. The incoming thread's context
         * is claimed by zeroing its stack_ptr, matching the kernel SMP pick
         * loop's "stack_ptr != nullptr" guard.
         */
        stack::element_t*
        switch_stacks (stack::element_t* sp)
        {
#if defined(OS_USE_SMP_SCHEDULER)
          // Mask local interrupts around context switch so higher-priority ISRs
          // cannot preempt PendSV and mutate ready_threads_list_ concurrently.
          uint32_t pri = __get_PRIMASK ();
          __disable_irq ();

          unsigned cpu = port_cpu_id ();

          // Enter the kernel spinlock (recursive).
          if (_smp_klock.owner != cpu)
            {
              _smp_klock_raw_acquire ();
              _smp_klock.owner = cpu;
            }
          _smp_klock.depth = _smp_klock.depth + 1;

          // Clear the PendSV bit (late-arrival/preemption safety).
          SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;

          rtos::thread* old_thread = rtos::scheduler::current_thread_[cpu];

          // Publish the outgoing thread's saved frame immediately (safe: we
          // run on the MSP, the outgoing PSP is no longer in use).
          old_thread->context_.port_.stack_ptr = sp;

          // Pick the next thread (affinity-aware, per CPU).
          rtos::scheduler::internal_switch_threads ();

          rtos::thread* new_thread = rtos::scheduler::current_thread_[cpu];

          if (new_thread == nullptr)
            {
              // No ready thread and no idle thread on this core — fatal.
              // Release the kernel spinlock before halting so other cores do not deadlock.
              _smp_klock.depth = _smp_klock.depth - 1;
              if (_smp_klock.depth == 0)
                {
                  _smp_klock.owner = SMP_NO_OWNER;
                  _smp_klock_raw_release ();
                }
              __set_PRIMASK (pri);
              __asm__ volatile ("cpsid if" ::: "memory");
              for (;;)
                __asm__ volatile ("wfi");
            }

          stack::element_t* out_sp;
          if (new_thread != old_thread)
            {
              out_sp = new_thread->context_.port_.stack_ptr;
              // Claim: new_thread's context is now live in this CPU's
              // registers; other cores must not pick it.
              new_thread->context_.port_.stack_ptr = nullptr;
            }
          else
            {
              out_sp = sp;
              old_thread->context_.port_.stack_ptr = nullptr;
            }

          // ISR-safe instrumentation (no printf): record the switch for the
          // fault handler to dump (globals declared file-scope, extern "C").
          g_boot_mark (6);
          g_sw_old = old_thread;
          g_sw_new = new_thread;
          g_sw_outsp = out_sp;
          g_sw_old_name = old_thread ? old_thread->name () : "?";
          g_sw_new_name = new_thread ? new_thread->name () : "?";
          g_sw_hist[g_sw_count & 7] = g_sw_new_name;
          g_sw_count = g_sw_count + 1;
          g_boot_mark (7);

          // Leave the kernel spinlock.
          _smp_klock.depth = _smp_klock.depth - 1;
          if (_smp_klock.depth == 0)
            {
              _smp_klock.owner = SMP_NO_OWNER;
              _smp_klock_raw_release ();
            }

          __set_PRIMASK (pri);

          return out_sp;
#else
          // Single-core: the ready lists are already safe (PendSV is the
          // lowest-priority exception), but mask IRQs anyway so a late-arrival
          // interrupt cannot observe a half-switched state.
          uint32_t pri = __get_PRIMASK ();
          __disable_irq ();

          SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;

          rtos::thread* old_thread = rtos::scheduler::current_thread_;
          old_thread->context_.port_.stack_ptr = sp;
          rtos::scheduler::internal_switch_threads ();
          stack::element_t* out_sp
              = rtos::scheduler::current_thread_->context_.port_.stack_ptr;

          __set_PRIMASK (pri);

          g_boot_mark (6);
          g_sw_old = old_thread;
          g_sw_new = rtos::scheduler::current_thread_;
          g_sw_outsp = out_sp;
          g_sw_old_name = old_thread ? old_thread->name () : "?";
          g_sw_new_name = rtos::scheduler::current_thread_
                              ? rtos::scheduler::current_thread_->name ()
                              : "?";
          g_sw_hist[g_sw_count & 7] = g_sw_new_name;
          g_sw_count = g_sw_count + 1;
          g_boot_mark (7);

          return out_sp;
#endif /* defined(OS_USE_SMP_SCHEDULER) */
        }

      } /* namespace scheduler */
    } /* namespace port */
  } /* namespace rtos */
} /* namespace os */

// ----------------------------------------------------------------------------

namespace os
{
  namespace rtos
  {
    namespace port
    {
      namespace scheduler
      {
        inline stack::element_t* __attribute__ ((always_inline))
        save_on_stack (void)
        {
          stack::element_t* sp_;

          asm volatile (
              // Get the thread stack
              " mrs %[r], PSP                       \n"
              " isb                                 \n"

#if defined(__ARM_FP)
              // A thread that has used the FPU (EXC_RETURN bit 4 clear): the
              // hardware stacked s0-s15 and the FPSCR; save s16-s31 too.
              // Being an FP instruction, this also completes a pending lazy
              // save into the frame, before the frame is published to the
              // other core. restore_from_stack() mirrors it exactly.
              " tst lr, #0x10                       \n"
              " it eq                               \n"
              " vstmdbeq %[r]!, {s16-s31}           \n"
#endif

              // Save the core registers r4-r11,r14. Decrement [r].
              " stmdb %[r]!, {r4-r9,sl,fp,lr}       \n"

              : [r] "=r"(sp_) /* out */
              : /* in */
              : /* clobber. DO NOT add anything here! */
          );

          return sp_;
        }

        inline void __attribute__ ((always_inline))
        restore_from_stack (stack::element_t* sp)
        {
          asm volatile (

              // Restore the core registers r4-r11,r14. Increment [r].
              " ldmia %[r]!, {r4-r9,sl,fp,lr}       \n"

#if defined(__ARM_FP)
              // The mirror of save_on_stack(), keyed on the restored lr.
              " tst lr, #0x10                       \n"
              " it eq                               \n"
              " vldmiaeq %[r]!, {s16-s31}           \n"
#endif

              // Restore the thread stack register.
              " msr PSP, %[r]                       \n"
              " isb                                 \n"

              : /* out */
              : [r] "r"(sp) /* in */
              : /* clobber. DO NOT add anything here! */
          );
        }
      } /* namespace scheduler */
    } /* namespace port */
  } /* namespace rtos */
} /* namespace os */

// ----------------------------------------------------------------------------

extern "C"
{
#if defined(OS_USE_SMP_SCHEDULER)
  // Current core index (SIO_CPUID). Provided to the kernel SMP branch.
  unsigned
  port_cpu_id (void)
  {
    return os::rtos::port::scheduler::port_cpu_id ();
  }

  // Cross-core IPI: wake another core over the SIO inter-core FIFO.
  // Non-blocking: if the FIFO is full the target already has a pending IPI.
  void
  port_smp_ipi (unsigned cpu)
  {
    if (cpu == port_cpu_id ())
      {
        return;
      }
    if (multicore::fifo_write_ready ())
      {
        multicore::fifo_push (0x4D53u); // "SM"
      }
  }

  // Core 1's handler-mode stack top, for multicore::launch_core1().
  unsigned
  port_core1_stack_top (void)
  {
    using os::rtos::port::scheduler::core1_msp_stack;
    return reinterpret_cast<unsigned> (
        &core1_msp_stack[sizeof (core1_msp_stack)
                         / sizeof (core1_msp_stack[0])]);
  }

  // Enable core 0's cross-core IPI channel. MUST be called only AFTER the
  // core-1 bootrom launch has completed: while the launch is in progress the
  // FIFO carries the bootrom handshake words, and a FIFO IRQ handler would
  // drain them away (stalling the launch forever). Core 1 enables its own in
  // port_smp_secondary_start().
  void
  port_smp_core0_ipi_enable (void)
  {
    while (rp2350::reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_VLD)
      {
        volatile uint32_t d = rp2350::reg (rp2350::SIO_FIFO_RD);
        (void) d;
      }
    rp2350::reg (rp2350::SIO_FIFO_ST) = 0x0C; // write-1-to-clear WOF|ROE
    NVIC_EnableIRQ (SIO_IRQ_FIFO_IRQn);
  }
  // Core-1 entry, entered from the bootrom FIFO launch (multicore.cpp).
  // Mirrors the A7 port_smp_secondary_start(): build a fake thread context
  // for core 1, start its own SysTick/PendSV, then reschedule into the
  // first core-1 runnable thread (or the idle1 thread registered in
  // os_idle_thread_core[1] before the launch).
  void
  port_smp_secondary_start (void)
  {
    using namespace os::rtos;

    __asm__ volatile ("cpsid i" ::: "memory");

    // Mirror the core-0 boot.S FPU bring-up on this core. boot.S only runs
    // on core 0; without this the first floating-point instruction on core 1
    // faults NOCP (CPACR resets to 0 on RP2350). ASPEN/LSPEN on (the reset
    // defaults): a thread that has used the FPU gets the extended exception
    // frame, and save_on_stack() adds s16-s31, so its FPU registers follow it
    // across switches and cores.
    rp2350::reg (rp2350::PPB_CPACR) = 0x00F00000u; // CP10/CP11 full access
    rp2350::reg (0xE000EF34) |= 0xC0000000u;       // FPCCR: set ASPEN|LSPEN
    __set_CONTROL (__get_CONTROL () & ~CONTROL_FPCA_Msk);
    __DSB ();
    __ISB ();

    unsigned cpu = port_cpu_id (); // 1

    // Per-core exceptions: PendSV + SysTick at the lowest priority.
    NVIC_SetPriority (PendSV_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
    SysTick_Config (SystemCoreClock / clock_systick::frequency_hz);
    NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);

    // Cross-core IPI channel: drain stale FIFO data (the core-1 launch used
    // the FIFO), clear the sticky WOF/ROE bits, enable the FIFO IRQ.
    while (rp2350::reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_VLD)
      {
        volatile uint32_t d = rp2350::reg (rp2350::SIO_FIFO_RD);
        (void) d;
      }
    rp2350::reg (rp2350::SIO_FIFO_ST) = 0x0C; // write-1-to-clear WOF|ROE
    NVIC_SetPriority (SIO_IRQ_FIFO_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
    NVIC_EnableIRQ (SIO_IRQ_FIFO_IRQn);

    // Fake "current" thread for core 1 (never restored). Done BEFORE the
    // PSP switch: `cpu` was read on the MSP, and any local alive across the
    // SPSEL change would otherwise be spilled/read from the wrong stack.
    static os_thread_t fake_thread_sec;
    memset (&fake_thread_sec, 0, sizeof (os_thread_t));
    fake_thread_sec.name = "fake_thread_sec";
    scheduler::current_thread_[cpu] = reinterpret_cast<thread*> (&fake_thread_sec);

    // Switch thread mode onto a PSP, like the core-0 start(). No locals are
    // used after this point.
    __set_PSP (__get_MSP ());
    __set_CONTROL (__get_CONTROL () | CONTROL_SPSEL_Msk);
    __ISB ();

    // Trigger the first PendSV; it will fire after __enable_irq().
    port::scheduler::reschedule ();

    __enable_irq ();

    for (;;)
      {
        __asm__ volatile ("wfi");
      }
  }
#endif /* defined(OS_USE_SMP_SCHEDULER) */
}

// ----------------------------------------------------------------------------

// SIO inter-core FIFO IRQ: cross-core reschedule. Drains the RX FIFO (all
// words), clears the sticky overflow flags, and requests a PendSV switch on
// this core. Called on the MSP in handler mode.
extern "C" void __attribute__ ((section (".after_vectors")))
SIO_IRQ_FIFO_Handler (void)
{
  while (rp2350::reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_VLD)
    {
      volatile uint32_t d = rp2350::reg (rp2350::SIO_FIFO_RD);
      (void) d;
    }
  rp2350::reg (rp2350::SIO_FIFO_ST) = 0x0C; // write-1-to-clear WOF|ROE

  os::rtos::port::scheduler::reschedule ();
}

// Core SysTick → RTOS tick. Only core 0 advances the clock
// (os_systick_handler(), which itself ends by rescheduling); core 1's
// SysTick only reschedules core 1 so it picks up threads woken by core 0.
// (Single-core build: this core simply drives the clock.)
extern "C" void __attribute__ ((section (".after_vectors")))
SysTick_Handler (void)
{
#if defined(OS_USE_SMP_SCHEDULER)
  if (port_cpu_id () == 0)
    {
      g_boot_mark (8);
      os_systick_handler ();
    }
  else
    {
      os::rtos::port::scheduler::reschedule ();
    }
#else
  g_boot_mark (8);
  os_systick_handler ();
#endif /* defined(OS_USE_SMP_SCHEDULER) */
}

// ----------------------------------------------------------------------------

// PendSV_Handler: the actual context switch. Naked, so the LR/PSP handling
// is fully controlled; runs on the MSP. extern "C" so the vector table (as
// on the Cortex-M port) binds the C symbol, not the mangled C++ name.
extern "C" void __attribute__ ((section (".after_vectors"), naked, used,
                                optimize ("1")))
PendSV_Handler (void)
{
  // Do NOT use push/pop, since the LR is from a different context.
  os::rtos::port::scheduler::restore_from_stack (
      os::rtos::port::scheduler::switch_stacks (
          os::rtos::port::scheduler::save_on_stack ()));

  asm volatile ("bx lr");
}

// ----------------------------------------------------------------------------
