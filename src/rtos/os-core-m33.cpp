/*
 * os-core-m33.cpp — GENERIC Cortex-M33 (SSE-200) port of the shared
 * micro-os-plus-iii kernel, for the emulated `pico2-1cpu` (mps2-an505) and
 * `2xcortex-m33` (mps2-an521) configurations.
 *
 * DUAL-BRANCH, gated on OS_USE_SMP_SCHEDULER:
 *
 *  - single-core (pico2-1cpu): the stock scalar reference scheduler — scalar
 *    current_thread_/lock_state, PRIMASK critical sections, no spinlock, no
 *    core-1 launch.
 *  - SMP (2xcortex-m33): the M33 SMP port, the RP2350 port's analogue with
 *    the RP2350 SIO replaced by the SSE-200 blocks QEMU models:
 *
 *      core id   SSE-200 CPU_IDENTITY, 0x4001F000 (each core reads its own)
 *      IPI       SSE-200 Message Handling Unit
 *                  MHU0 @ 0x40003000 -> CPU1 IRQ 6 (core 1's inbox)
 *                  MHU1 @ 0x40004000 -> CPU0 IRQ 7 (core 0's inbox)
 *      lock      an LDREX/STREX word (see include-m33/.../os-inlines.h)
 *      core 1    released by clearing CPUWAIT bit 1 in the SSE-200 System
 *                Control (0x50021000 + 0x118); its reset vector is
 *                INITSVTOR1 (0x50021000 + 0x114), which we point at a small
 *                secondary vector table whose Reset is
 *                port_smp_secondary_start().
 */

#include <cmsis-plus/rtos/os.h>
#include <cmsis-plus/diag/trace.h>

#include <cmsis_device.h>

#include <cstring>

// ----------------------------------------------------------------------------
// SSE-200 register block.

#define SSE200_CPUID_BASE 0x4001F000u
#define SSE200_MHU0_BASE 0x40003000u
#define SSE200_MHU1_BASE 0x40004000u
#define SSE200_SYSCTL_BASE 0x50021000u

#define SSE200_MHU_CPU0INTR_SET 0x004u
#define SSE200_MHU_CPU0INTR_CLR 0x008u
#define SSE200_MHU_CPU1INTR_SET 0x014u
#define SSE200_MHU_CPU1INTR_CLR 0x018u

#define SSE200_SYSCTL_INITSVTOR1 0x114u
#define SSE200_SYSCTL_CPUWAIT 0x118u

#define SSE200_MHU_IRQ_CORE0 7 // MHU1 cpu0irq
#define SSE200_MHU_IRQ_CORE1 6 // MHU0 cpu1irq

// Enable automatic and lazy FPU state stacking (FPCCR.ASPEN, LSPEN: the reset
// defaults, set again in case something cleared them). A thread that has used
// the FPU then gets the extended exception frame (s0-s15, FPSCR) and
// EXC_RETURN bit 4 clear, and PendSV saves s16-s31 on its stack as well
// (save_on_stack/restore_from_stack). Without it, the FPU registers belong to
// the core: two threads using them on one core, or one that moves to the
// other core, see each other's values. CONTROL.FPCA is cleared: no FP context
// is active yet at boot.
static inline void
m33_fpu_enable_stacking (void)
{
#if defined(__ARM_FP)
  FPU->FPCCR |= (FPU_FPCCR_ASPEN_Msk | FPU_FPCCR_LSPEN_Msk);
  __set_CONTROL (__get_CONTROL () & ~CONTROL_FPCA_Msk);
  __DSB ();
  __ISB ();
#endif
}

static inline volatile uint32_t&
sse200_reg (uintptr_t addr)
{
  return *reinterpret_cast<volatile uint32_t*> (addr);
}

// ----------------------------------------------------------------------------
// Instrumentation the pico2 board's rtos-glue.cpp provides; here they are
// local no-ops so the generic core has no board dependency.

volatile void* g_sw_old;
volatile void* g_sw_new;
volatile void* g_sw_outsp;
const char* g_sw_old_name;
const char* g_sw_new_name;
volatile unsigned long g_sw_count;
const char* g_sw_hist[8];

static inline void
g_boot_mark (unsigned)
{
  ;
}

#if defined(OS_USE_SMP_SCHEDULER)
// Core 1's idle body, so the SMP scheduler always has a thread to run there.
static void*
m33_secondary_idle (void*)
{
  for (;;)
    {
      __asm__ volatile ("wfi");
    }
}
#endif

extern "C"
{
  extern unsigned int _Heap_Limit;
  extern unsigned int __stack;
  void os_systick_handler (void); // kernel: src/rtos/os-clocks.cpp
#if defined(OS_USE_SMP_SCHEDULER)
  void port_smp_launch_core1 (void);
  void port_smp_core0_ipi_enable (void);
#endif
}

// The kernel's core-0 idle thread and the per-core idle array.
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

// Primary interrupt vector table, provided by the device (device-qemu-cortexm).
extern "C" void (*_interrupt_vectors[]) (void);

#if defined(OS_USE_SMP_SCHEDULER)
// Secondary vector table, defined at the bottom of this file; core 1's reset
// vector (INITSVTOR1) points at it.
extern "C" const uint32_t _secondary_vectors[];
#endif

namespace os
{
  namespace rtos
  {
    namespace port
    {
      namespace stack
      {
        // Stack frame, as used by PendSV.
        typedef struct frame_s
        {
          stack::element_t r4;
          stack::element_t r5;
          stack::element_t r6;
          stack::element_t r7;
          stack::element_t r8;
          stack::element_t r9;
          stack::element_t r10_sl;
          stack::element_t r11_fp;
          stack::element_t r14_exec_return;
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

      void
      context::create (void* context, void* func, void* args)
      {
        class rtos::thread::context* th_ctx
            = static_cast<class rtos::thread::context*> (context);

        class rtos::thread::stack& stack = th_ctx->stack ();

        rtos::thread::stack::element_t* p = stack.top ();

        assert ((p - stack.bottom ())
                > static_cast<int> (2 * sizeof (stack::frame_t)));

        p -= (sizeof (stack::frame_t)
              / sizeof (rtos::thread::stack::element_t));

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
        NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
      }

      // ----------------------------------------------------------------------

      namespace scheduler
      {
#if defined(OS_USE_SMP_SCHEDULER)
        state_t lock_state[OS_NCPU];
        uint32_t lock_primask[OS_NCPU];
        smp_klock_t _smp_klock = { 0, SMP_NO_OWNER, 0 };

        // Core 1's handler-mode (MSP) stack. The reset vector table's first
        // entry points at its top, so core 1 has a valid MSP before any C
        // runs (see _secondary_vectors below).
        stack::element_t core1_msp_stack[1024] __attribute__ ((aligned (8)));
#else
        state_t lock_state;
#endif /* defined(OS_USE_SMP_SCHEDULER) */

        result_t
        initialize (void)
        {
          __disable_irq ();

          return result::ok;
        }

        void
        start (void)
        {
          __disable_irq ();
          g_boot_mark (1);

#if defined(__ARM_FP)
          // Enable the FPU on this core. The kernel's early hardware init
          // normally does this, but it is gated on the startup library's own
          // flags; do it here too so a hard-float image can never fault NOCP.
          SCB->CPACR |= (0xF << 20); // CP10/CP11 full access
          __DSB ();
          __ISB ();
          m33_fpu_enable_stacking ();
#endif

          // Switch thread mode onto a PSP.
          __set_PSP (__get_MSP ());
          __set_CONTROL (__get_CONTROL () | CONTROL_SPSEL_Msk);
          __ISB ();
          g_boot_mark (2);

          // Re-initialise MSP to the reset value (the vector table's word 0).
          __set_MSP (*(reinterpret_cast<uint32_t*> (SCB->VTOR)));

          // The interrupt stack: from the heap limit up to the main stack.
          rtos::interrupts::stack ()->set (
              reinterpret_cast<stack::element_t*> (&_Heap_Limit),
              static_cast<size_t> (&__stack - &_Heap_Limit)
                  * sizeof (__stack));
          {
            const std::size_t n
                = static_cast<std::size_t> (&__stack - &_Heap_Limit)
                  / sizeof (stack::element_t);
            stack::element_t* p
                = reinterpret_cast<stack::element_t*> (&_Heap_Limit);
            for (std::size_t i = 0; i < n; ++i)
              {
                p[i] = os::rtos::thread::stack::magic;
              }
          }

          NVIC_SetPriority (PendSV_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
          g_boot_mark (3);

          static os_thread_t fake_thread;
          std::memset (&fake_thread, 0, sizeof (os_thread_t));
          fake_thread.name = "fake_thread";
#if defined(OS_USE_SMP_SCHEDULER)
          rtos::scheduler::current_thread_[0]
              = reinterpret_cast<rtos::thread*> (&fake_thread);

          lock_state[0] = state::init;
          lock_state[1] = state::init;
          rtos::scheduler::os_idle_thread_core[0] = ::os_idle_thread;

          // Core 1's idle thread, registered before the core is released so
          // the SMP picker always finds something to run there.
          static os::rtos::thread::stack::element_t idle1_stack[1024]
              __attribute__ ((aligned (8)));
          {
            os::rtos::thread::attributes attr = os::rtos::thread::initializer;
            attr.th_stack_address = idle1_stack;
            attr.th_stack_size_bytes = sizeof (idle1_stack);
            static os::rtos::thread* idle1
                = new os::rtos::thread ("idle1", m33_secondary_idle, nullptr,
                                        attr);
            rtos::scheduler::os_idle_thread_core[1] = idle1;
          }

          // Core 0's IPI inbox is the MHU1 IRQ (7). Leave it disabled; the
          // launcher enables it (port_smp_core0_ipi_enable()) after core 1 is
          // running.
          NVIC_SetPriority (static_cast<IRQn_Type> (SSE200_MHU_IRQ_CORE0),
                            (1UL << __NVIC_PRIO_BITS) - 1UL);
#else
          rtos::scheduler::current_thread_
              = reinterpret_cast<rtos::thread*> (&fake_thread);

          lock_state = state::init;
#endif /* defined(OS_USE_SMP_SCHEDULER) */

          __set_BASEPRI (0);

          scheduler::reschedule ();
          g_boot_mark (4);

#if defined(OS_USE_SMP_SCHEDULER)
          // Release core 1 and open its IPI inbox on core 0. Done with IRQs
          // still masked on core 0 so the first switch happens on a fully
          // initialised scheduler.
          port_smp_launch_core1 ();
          port_smp_core0_ipi_enable ();
#endif

          __enable_irq ();
          g_boot_mark (5);

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
                  // Take the kernel spinlock, then publish "locked". IRQs
                  // stay masked until lock_state is set, so no handler can
                  // observe a locked core that is not yet locked. The PRIMASK
                  // to restore is the caller's, read before the cpsid above.
                  (void)port_set_lock ();
                  lock_primask[cpu] = pri;
                  lock_state[cpu] = state;
                }
              else
                {
                  // Publish "unlocked" BEFORE releasing the spinlock and
                  // restoring PRIMASK. Otherwise a switch taken in the window
                  // (klock released, IRQs enabled, lock_state still locked)
                  // hands the next thread on this core a stale "locked" flag:
                  // its sleep_for()/mutex::lock() trips !scheduler::locked(),
                  // and the pending reschedule is lost. The AArch32/AArch64
                  // ports clear lock_state first for the same reason.
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

        void
        reschedule (void)
        {
          if (rtos::scheduler::locked ()
              || (rtos::interrupts::in_handler_mode ()
                  && !rtos::scheduler::preemptive ()))
            {
              return;
            }

          SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
          __DSB ();
          __ISB ();
        }

        // --------------------------------------------------------------------

        stack::element_t*
        switch_stacks (stack::element_t* sp)
        {
#if defined(OS_USE_SMP_SCHEDULER)
          // Mask local interrupts around context switch so higher-priority ISRs
          // cannot preempt PendSV and mutate ready_threads_list_ concurrently.
          uint32_t pri = __get_PRIMASK ();
          __disable_irq ();

          unsigned cpu = port_cpu_id ();

          if (_smp_klock.owner != cpu)
            {
              _smp_klock_raw_acquire ();
              _smp_klock.owner = cpu;
            }
          _smp_klock.depth = _smp_klock.depth + 1;

          SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;

          rtos::thread* old_thread = rtos::scheduler::current_thread_[cpu];
          old_thread->context_.port_.stack_ptr = sp;

          rtos::scheduler::internal_switch_threads ();

          rtos::thread* new_thread = rtos::scheduler::current_thread_[cpu];

          if (new_thread == nullptr)
            {
              _smp_klock.depth = _smp_klock.depth - 1;
              if (_smp_klock.depth == 0)
                {
                  _smp_klock.owner = SMP_NO_OWNER;
                  _smp_klock_raw_release ();
                }
              // Stay masked: the scheduler state is already torn enough that
              // an IRQ handler calling an RTOS API must not see it. `pri` is
              // deliberately not restored here.
              __asm__ volatile ("cpsid if" ::: "memory");
              for (;;)
                __asm__ volatile ("wfi");
            }

          stack::element_t* out_sp;
          if (new_thread != old_thread)
            {
              out_sp = new_thread->context_.port_.stack_ptr;
              new_thread->context_.port_.stack_ptr = nullptr;
            }
          else
            {
              out_sp = sp;
              old_thread->context_.port_.stack_ptr = nullptr;
            }

          g_boot_mark (6);
          g_sw_old = old_thread;
          g_sw_new = new_thread;
          g_sw_outsp = out_sp;
          g_sw_old_name = old_thread ? old_thread->name () : "?";
          g_sw_new_name = new_thread ? new_thread->name () : "?";
          g_sw_hist[g_sw_count & 7] = g_sw_new_name;
          g_sw_count = g_sw_count + 1;
          g_boot_mark (7);

          _smp_klock.depth = _smp_klock.depth - 1;
          if (_smp_klock.depth == 0)
            {
              _smp_klock.owner = SMP_NO_OWNER;
              _smp_klock_raw_release ();
            }

          __set_PRIMASK (pri);

          return out_sp;
#else
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
              " mrs %[r], PSP                       \n"
              " isb                                 \n"
#if defined(__ARM_FP)
              // A thread that has used the FPU (EXC_RETURN bit 4 clear): the
              // hardware stacked s0-s15 and the FPSCR; save s16-s31 too.
              // Being an FP instruction, this also completes a pending lazy
              // save into the frame, before the frame is published.
              " tst lr, #0x10                       \n"
              " it eq                               \n"
              " vstmdbeq %[r]!, {s16-s31}           \n"
#endif
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
              " ldmia %[r]!, {r4-r9,sl,fp,lr}       \n"
#if defined(__ARM_FP)
              // The mirror of save_on_stack(), keyed on the restored lr.
              " tst lr, #0x10                       \n"
              " it eq                               \n"
              " vldmiaeq %[r]!, {s16-s31}           \n"
#endif
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
  unsigned
  port_cpu_id (void)
  {
    return os::rtos::port::scheduler::port_cpu_id ();
  }

  // Cross-core IPI over the SSE-200 MHU.
  void
  port_smp_ipi (unsigned cpu)
  {
    if (cpu == port_cpu_id ())
      {
        return;
      }
    if (cpu == 0)
      {
        // Ask core 0 to reschedule: core 0's inbox is MHU1.
        sse200_reg (SSE200_MHU1_BASE + SSE200_MHU_CPU0INTR_SET) = 1;
      }
    else if (cpu == 1)
      {
        // Ask core 1 to reschedule: core 1's inbox is MHU0.
        sse200_reg (SSE200_MHU0_BASE + SSE200_MHU_CPU1INTR_SET) = 1;
      }
  }

  // Enable core 0's IPI inbox (MHU1 IRQ 7). Called once, after core 1 is up.
  void
  port_smp_core0_ipi_enable (void)
  {
    // Clear any stale notification before unmasking.
    sse200_reg (SSE200_MHU1_BASE + SSE200_MHU_CPU0INTR_CLR) = 0xFu;
    NVIC_SetPriority (static_cast<IRQn_Type> (SSE200_MHU_IRQ_CORE0),
                      (1UL << __NVIC_PRIO_BITS) - 1UL);
    NVIC_EnableIRQ (static_cast<IRQn_Type> (SSE200_MHU_IRQ_CORE0));
  }

  // Release core 1 from reset. Its reset vector is the secondary table, whose
  // Reset is port_smp_secondary_start().
  void
  port_smp_launch_core1 (void)
  {
    // INITSVTOR1 -> the secondary vector table.
    sse200_reg (SSE200_SYSCTL_BASE + SSE200_SYSCTL_INITSVTOR1)
        = reinterpret_cast<uintptr_t> (_secondary_vectors);
    __DSB ();
    // Clear CPUWAIT: powers up CPU1 (CPU0 is already running).
    sse200_reg (SSE200_SYSCTL_BASE + SSE200_SYSCTL_CPUWAIT) = 0;
    __DSB ();
  }

  // Core 1's entry, from the secondary vector table's Reset slot.
  void
  port_smp_secondary_start (void)
  {
    using namespace os::rtos;

    __asm__ volatile ("cpsid i" ::: "memory");

    // Adopt the primary vector table (SysTick, PendSV, MHU0).
    SCB->VTOR = reinterpret_cast<uint32_t> (_interrupt_vectors);
    __DSB ();
    __ISB ();

    // FPU bring-up (core 0 did this in SystemInit; core 1 never ran it).
    SCB->CPACR |= (0xFu << 20); // CP10/CP11 full access
    __DSB ();
    __ISB ();
    m33_fpu_enable_stacking ();

    unsigned cpu = port_cpu_id (); // 1

    NVIC_SetPriority (PendSV_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
    SysTick_Config (SystemCoreClock / clock_systick::frequency_hz);
    NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);

    // Core 1's IPI inbox is the MHU0 IRQ (6).
    sse200_reg (SSE200_MHU0_BASE + SSE200_MHU_CPU1INTR_CLR) = 0xFu;
    NVIC_SetPriority (static_cast<IRQn_Type> (SSE200_MHU_IRQ_CORE1),
                      (1UL << __NVIC_PRIO_BITS) - 1UL);
    NVIC_EnableIRQ (static_cast<IRQn_Type> (SSE200_MHU_IRQ_CORE1));

    static os_thread_t fake_thread_sec;
    std::memset (&fake_thread_sec, 0, sizeof (os_thread_t));
    fake_thread_sec.name = "fake_thread_sec";
    scheduler::current_thread_[cpu]
        = reinterpret_cast<thread*> (&fake_thread_sec);

    // Switch thread mode onto a PSP, like core 0's start().
    __set_PSP (__get_MSP ());
    __set_CONTROL (__get_CONTROL () | CONTROL_SPSEL_Msk);
    __ISB ();

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
// The secondary vector table (used only by core 1 on the SMP image). Its first
// word is core 1's initial MSP; the Reset slot is port_smp_secondary_start().

#if defined(OS_USE_SMP_SCHEDULER)
extern "C" void port_smp_secondary_start (void);
extern "C" __attribute__ ((section (".isr_vector_secondary"), used,
                           aligned (512)))
const uint32_t _secondary_vectors[] = {
  reinterpret_cast<uint32_t> (
      &os::rtos::port::scheduler::core1_msp_stack
           [sizeof (os::rtos::port::scheduler::core1_msp_stack)
            / sizeof (os::rtos::port::scheduler::core1_msp_stack[0])]),
  reinterpret_cast<uint32_t> (&port_smp_secondary_start),
};
#endif

// ----------------------------------------------------------------------------

#if defined(OS_USE_SMP_SCHEDULER)
// MHU1 IRQ (7): core 0's inbox. Clears MHU1's CPU0 notification and reschedules.
extern "C" void __attribute__ ((section (".after_vectors")))
MHU1_IRQHandler (void)
{
  sse200_reg (SSE200_MHU1_BASE + SSE200_MHU_CPU0INTR_CLR) = 0xFu;
  os::rtos::port::scheduler::reschedule ();
}

// MHU0 IRQ (6): core 1's inbox.
extern "C" void __attribute__ ((section (".after_vectors")))
MHU0_IRQHandler (void)
{
  sse200_reg (SSE200_MHU0_BASE + SSE200_MHU_CPU1INTR_CLR) = 0xFu;
  os::rtos::port::scheduler::reschedule ();
}
#endif

// Core SysTick -> RTOS tick. Only core 0 advances the clock; core 1's SysTick
// only reschedules core 1 so it picks up threads woken by core 0.
//
// SMP only: the single-core image keeps the device's plain SysTick_Handler
// (device-qemu-cortexm/src/exception-handlers.cpp).
#if defined(OS_USE_SMP_SCHEDULER)
extern "C" void __attribute__ ((section (".after_vectors")))
SysTick_Handler (void)
{
  if (port_cpu_id () == 0)
    {
      g_boot_mark (8);
      os_systick_handler ();
    }
  else
    {
      os::rtos::port::scheduler::reschedule ();
    }
}
#endif /* defined(OS_USE_SMP_SCHEDULER) */

// ----------------------------------------------------------------------------

// PendSV_Handler: the actual context switch.
extern "C" void __attribute__ ((section (".after_vectors"), naked, used,
                                optimize ("1")))
PendSV_Handler (void)
{
  os::rtos::port::scheduler::restore_from_stack (
      os::rtos::port::scheduler::switch_stacks (
          os::rtos::port::scheduler::save_on_stack ()));

  asm volatile ("bx lr");
}

// ----------------------------------------------------------------------------
