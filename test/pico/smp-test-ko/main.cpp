/*
 * smp-test-ko (Pico / RP2040B) — SMP kernel-object test (2× Cortex-M0+).
 *
 * The SMP twin of sp-test-ko: OS_USE_SMP_SCHEDULER is defined, so the shared
 * micro-os-plus-iii kernel runs on BOTH cores with the pico hand-written M0+
 * SMP port (per-core PendSV/SysTick, the recursive SIO-spinlock kernel lock,
 * the SIO-FIFO cross-core IPI, the bootrom core-1 launch).
 *
 * The same ten sub-tests as sp-test-ko, but the "controller" thread is pinned
 * to core 0 and the "helper" thread to core 1, so every blocking hand-off is a
 * genuine CROSS-CORE interaction — and T1 hammers one mutex from both cores at
 * once (the real test of the SIO-spinlock-backed critical section):
 *
 *   T1  mutex               mutual-exclusion counter, both cores (2×1000)
 *   T2  mutex               try_lock() busy while the other core holds it
 *   T3  recursive mutex     nested lock/unlock + reacquire (core 0)
 *   T4  counting semaphore  cross-core produce/consume; empty try_wait fails
 *   T5  binary semaphore    cross-core post/wait signal
 *   T6  event flags         cross-core raise/wait masked + auto-clear
 *   T7  message queue       cross-core send/receive round-trip + payload verify
 *   T8  memory pool         alloc/exhaust/free/reuse + capacity (core 0)
 *   T9  condition variable  cross-core predicate wait + signal
 *   T10 periodic timer      ~20 firings in ~205 ms (core-0 tick)
 *
 * Visualisation:
 *   - UART0 @115200: one "PASS/FAIL core=c <name>" line per sub-test, then a
 *     final "RESULT: PASS/FAIL (p passed, f failed)".
 *   - LED (GPIO25): a short blink per passed sub-test (triple-blink on a
 *     failure); after the summary, slow heartbeat = all PASS, fast = some FAIL.
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/led.hpp>
#include <bsp/multicore.hpp>
#include <bsp/rp2040.hpp>
#include <bsp/uart.hpp>
#include <hw_result.hpp>
#include <cmsis_device.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>

using namespace os::rtos;

extern "C"
{
  void port_smp_secondary_start (void);
  unsigned port_core1_stack_top (void);
  unsigned port_cpu_id (void);
  void port_smp_core0_ipi_enable (void);
}

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

namespace
{
  // ---- Objects under test -------------------------------------------------
  mutex mtx{ "mtx" };
  mutex rmtx{ "rmtx", mutex::initializer_recursive };
  semaphore_counting csem{ "csem", 4, 0 };
  semaphore_binary bsem{ "bsem", 0 };
  event_flags ef{ "ef" };

  struct msg_t
  {
    std::uint32_t seq;
    std::uint32_t data;
  };
  message_queue mq_c2w{ "c2w", 8, sizeof (msg_t) };
  message_queue mq_w2c{ "w2c", 8, sizeof (msg_t) };
  memory_pool mp{ "mp", 4, sizeof (msg_t) };

  condition_variable cv{ "cv" };
  mutex cvm{ "cvm" };

  // ---- Cross-core handshake + results -------------------------------------
  semaphore_counting sem_go{ "go", 8, 0 };
  semaphore_counting sem_ack{ "ack", 8, 0 };

  enum stage_t
  {
    ST_MUTEX_COUNT = 1,
    ST_TRYLOCK,
    ST_CSEM,
    ST_BSEM,
    ST_EVF,
    ST_MQ,
    ST_CV,
    ST_DONE
  };
  volatile int g_stage = 0;

  volatile unsigned g_counter = 0;
  volatile int g_helper_trylock_busy = 0;
  volatile unsigned g_helper_csem_got = 0;
  volatile int g_helper_bsem_ok = 0;
  volatile int g_helper_ef_ok = 0;
  volatile int g_helper_mq_ok = 0;
  volatile int g_helper_cv_ok = 0;
  volatile bool g_cv_pred = false;
  volatile unsigned g_timer_fires = 0;

  constexpr unsigned COUNT_N = 1000;

  mutex g_log{ "log" };
  volatile unsigned g_pass = 0;
  volatile unsigned g_fail = 0;

  void
  logs (const char* s)
  {
    g_log.lock ();
    uart::write (s);
    g_log.unlock ();
  }

  void
  check (bool ok, const char* name)
  {
    char b[96];
    int n = std::snprintf (b, sizeof (b), "[%8lu ms] %-4s core=%u %s\n",
                           static_cast<unsigned long> (sysclock.now ()),
                           ok ? "PASS" : "FAIL", port_cpu_id (), name);
    g_log.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_log.unlock ();

    if (ok)
      {
        ++g_pass;
        led::on ();
        sysclock.sleep_for (40);
        led::off ();
        sysclock.sleep_for (40);
      }
    else
      {
        ++g_fail;
        for (int i = 0; i < 3; ++i)
          {
            led::on ();
            sysclock.sleep_for (60);
            led::off ();
            sysclock.sleep_for (60);
          }
      }
  }

  void
  tmr_cb (timer::func_args_t)
  {
    ++g_timer_fires;
  }

  // ---- core-1 idle thread (installed before the launch) -------------------
  void*
  secondary_idle_func (void*)
  {
    this_thread::thread ().priority (thread::priority::idle);
    for (;;)
      {
        __asm__ volatile ("dsb" ::: "memory");
        __asm__ volatile ("wfi");
        this_thread::yield ();
      }
    return nullptr;
  }

  // ---- Helper thread (pinned to core 1): the cross-core second party ------
  void*
  helper (void*)
  {
    for (;;)
      {
        sem_go.wait ();
        int s = g_stage;
        if (s == ST_DONE)
          {
            break;
          }
        switch (s)
          {
          case ST_MUTEX_COUNT:
            for (unsigned i = 0; i < COUNT_N; ++i)
              {
                mtx.lock ();
                ++g_counter;
                mtx.unlock ();
              }
            break;

          case ST_TRYLOCK:
            g_helper_trylock_busy = (mtx.try_lock () != result::ok) ? 1 : 0;
            break;

          case ST_CSEM:
            g_helper_csem_got = 0;
            for (int i = 0; i < 3; ++i)
              {
                csem.wait ();
                ++g_helper_csem_got;
              }
            break;

          case ST_BSEM:
            g_helper_bsem_ok = (bsem.wait () == result::ok) ? 1 : 0;
            break;

          case ST_EVF:
            {
              flags::mask_t of = 0;
              g_helper_ef_ok
                  = (ef.wait ((1u << 0), &of,
                              flags::mode::all | flags::mode::clear)
                     == result::ok)
                        ? 1
                        : 0;
              ef.raise (1u << 2);
            }
            break;

          case ST_MQ:
            {
              msg_t m{};
              g_helper_mq_ok = 0;
              if (mq_c2w.receive (&m, sizeof (m)) == result::ok)
                {
                  m.data ^= 0xFFFFu;
                  if (mq_w2c.send (&m, sizeof (m)) == result::ok)
                    {
                      g_helper_mq_ok = 1;
                    }
                }
            }
            break;

          case ST_CV:
            cvm.lock ();
            while (!g_cv_pred)
              {
                cv.wait (cvm);
              }
            cvm.unlock ();
            g_helper_cv_ok = 1;
            break;

          default:
            break;
          }
        sem_ack.post ();
      }
    return nullptr;
  }

  // ---- Controller thread (pinned to core 0) -------------------------------
  void*
  controller (void*)
  {
    logs ("\n");
    logs ("=============================================\n");
    logs ("  Pico / RP2040B - micro-os-plus-iii\n");
    logs ("  smp-test-ko : kernel objects, dual core\n");
    logs ("=============================================\n");

    // T1 — mutex: mutual exclusion, both cores hammering one lock in parallel.
    g_counter = 0;
    g_stage = ST_MUTEX_COUNT;
    sem_go.post ();
    for (unsigned i = 0; i < COUNT_N; ++i)
      {
        mtx.lock ();
        ++g_counter;
        mtx.unlock ();
      }
    sem_ack.wait ();
    check (g_counter == 2 * COUNT_N, "mutex: mutual-exclusion counter (2 cores)");

    // T2 — mutex: try_lock() busy while the OTHER CORE holds it.
    mtx.lock ();
    g_stage = ST_TRYLOCK;
    sem_go.post ();
    sem_ack.wait ();
    mtx.unlock ();
    check (g_helper_trylock_busy == 1, "mutex: try_lock busy (cross-core)");

    // T3 — recursive mutex (controller/core-0 only).
    {
      bool ok = (rmtx.lock () == result::ok);
      ok = ok && (rmtx.lock () == result::ok);
      ok = ok && (rmtx.unlock () == result::ok);
      ok = ok && (rmtx.unlock () == result::ok);
      ok = ok && (rmtx.try_lock () == result::ok);
      rmtx.unlock ();
      check (ok, "recursive mutex: nested lock + reacquire");
    }

    // T4 — counting semaphore, cross-core.
    g_stage = ST_CSEM;
    sem_go.post ();
    sysclock.sleep_for (5);
    csem.post ();
    csem.post ();
    csem.post ();
    sem_ack.wait ();
    check (g_helper_csem_got == 3 && csem.try_wait () != result::ok,
           "counting semaphore: 3 consumed + empty try_wait");

    // T5 — binary semaphore, cross-core.
    g_stage = ST_BSEM;
    sem_go.post ();
    sysclock.sleep_for (5);
    bsem.post ();
    sem_ack.wait ();
    check (g_helper_bsem_ok == 1, "binary semaphore: post/wait (cross-core)");

    // T6 — event flags, cross-core.
    {
      ef.clear (0xFFFFFFFFu);
      g_stage = ST_EVF;
      sem_go.post ();
      sysclock.sleep_for (5);
      ef.raise ((1u << 0) | (1u << 1));
      flags::mask_t of = 0;
      bool w = (ef.wait ((1u << 2), &of,
                         flags::mode::all | flags::mode::clear)
                == result::ok);
      sem_ack.wait ();
      bool tw = (ef.try_wait ((1u << 5)) != result::ok);
      check (g_helper_ef_ok == 1 && w && tw,
             "event flags: raise/wait mask + try_wait");
    }

    // T7 — message queue, cross-core round-trip + payload verify.
    {
      msg_t m{ 0x1234u, 0x00AAu };
      g_stage = ST_MQ;
      sem_go.post ();
      mq_c2w.send (&m, sizeof (m));
      msg_t r{};
      bool ok = (mq_w2c.receive (&r, sizeof (r)) == result::ok);
      sem_ack.wait ();
      ok = ok && g_helper_mq_ok == 1 && r.seq == 0x1234u
           && r.data == (0x00AAu ^ 0xFFFFu);
      check (ok, "message queue: send/receive + payload verify");
    }

    // T8 — memory pool (controller/core-0 only).
    {
      void* b[4];
      bool ok = true;
      for (int i = 0; i < 4; ++i)
        {
          b[i] = mp.try_alloc ();
          ok = ok && (b[i] != nullptr);
        }
      ok = ok && (mp.try_alloc () == nullptr);
      mp.free (b[0]);
      void* reuse = mp.try_alloc ();
      ok = ok && (reuse != nullptr);
      mp.free (reuse);
      for (int i = 1; i < 4; ++i)
        {
          mp.free (b[i]);
        }
      ok = ok && (mp.capacity () == 4);
      check (ok, "memory pool: alloc/exhaust/free/reuse + capacity");
    }

    // T9 — condition variable, cross-core.
    {
      g_cv_pred = false;
      g_stage = ST_CV;
      sem_go.post ();
      sysclock.sleep_for (10);
      cvm.lock ();
      g_cv_pred = true;
      cv.signal ();
      cvm.unlock ();
      sem_ack.wait ();
      check (g_helper_cv_ok == 1, "condition variable: predicate wait + signal");
    }

    // T10 — periodic timer (core-0 tick context).
    {
      g_timer_fires = 0;
      timer t{ "t", tmr_cb, nullptr, timer::periodic_initializer };
      t.start (10);
      sysclock.sleep_for (205);
      t.stop ();
      unsigned f = g_timer_fires;
      check (f >= 15 && f <= 25, "periodic timer: ~20 fires in 205 ms");
    }

    g_stage = ST_DONE;
    sem_go.post ();

    char b[80];
    int n = std::snprintf (b, sizeof (b),
                           "\nRESULT: %s  (%u passed, %u failed)\n",
                           g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    logs ("---------------------------------------------\n");
    g_log.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_log.unlock ();

    // End the run with the verdict (semihosting exit).
    if (g_fail == 0)
      {
        hw_result::ok ();
      }
    else
      {
        hw_result::fail ();
      }

    for (;;)
      {
        if (g_fail == 0)
          {
            led::on ();
            sysclock.sleep_for (900);
            led::off ();
            sysclock.sleep_for (100);
          }
        else
          {
            led::on ();
            sysclock.sleep_for (120);
            led::off ();
            sysclock.sleep_for (120);
          }
      }
    return nullptr;
  }

  thread::stack::element_t ctl_stack[2048];
  thread::stack::element_t hlp_stack[2048];
  thread::stack::element_t idle1_stack[512];
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  // Pin os_main/setup work to core 0.
  this_thread::thread ().cpu_affinity (1u << 0);

  led::init ();

  thread::attributes attr = thread::initializer;

  // IMPORTANT: launch core 1 BEFORE creating the test threads. The controller
  // (core 0) and helper (core 1) are same-priority, so once created either can
  // preempt os_main; if that happened before launch_core1() ran, core 1 would
  // never start, the core-1-pinned helper could never run, and the controller
  // would block forever on the first cross-core hand-off. So: install the
  // core-1 idle, launch core 1, THEN create helper and controller.

  // Core-1 idle thread must exist before the launch (per-core idle). It is
  // core-1-pinned, so it cannot run on core 0 and cannot preempt this setup.
  attr.th_stack_address = idle1_stack;
  attr.th_stack_size_bytes = sizeof (idle1_stack);
  static thread idle1{ "idle1", secondary_idle_func, nullptr, attr };
  os::rtos::scheduler::os_idle_thread_core[1] = &idle1;

  logs ("core 0: launching core 1 (bootrom FIFO handshake)...\n");
  multicore::launch_core1 (port_smp_secondary_start, port_core1_stack_top ());
  port_smp_core0_ipi_enable ();
  logs ("core 0: core 1 up; creating controller/helper...\n");

  // Helper first (core 1), then controller (core 0): the helper already exists
  // and core 1 is running by the time the controller starts its tests.
  attr.th_stack_address = hlp_stack;
  attr.th_stack_size_bytes = sizeof (hlp_stack);
  static thread hlp{ "helper", helper, nullptr, attr };
  hlp.cpu_affinity (1u << 1); // core 1

  attr.th_stack_address = ctl_stack;
  attr.th_stack_size_bytes = sizeof (ctl_stack);
  static thread ctl{ "controller", controller, nullptr, attr };
  ctl.cpu_affinity (1u << 0); // core 0

  logs ("core 0: SMP running.\n");

  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
