/*
 * sc-test-ko (Pico / RP2040B) — SINGLE-CORE kernel-object test.
 *
 * "sc" = single core. OS_USE_SMP_SCHEDULER is NOT defined, so the shared
 * micro-os-plus-iii kernel runs on ONE Cortex-M0+ core (core 0) with its stock
 * scalar reference scheduler on top of the pico M0+ port (single-core branch)
 * — exactly like the upstream Cortex-M port.
 *
 * It exercises EVERY kernel object in one main.cpp, each as a self-checking
 * round-trip. A "controller" thread runs the sub-tests sequentially; a
 * "helper" thread provides the second party for the blocking/hand-off paths
 * (both threads live on core 0 and interleave via the scheduler).
 *
 *   T1  mutex               mutual-exclusion counter (2×1000 increments)
 *   T2  mutex               try_lock() reports busy when held by another thread
 *   T3  recursive mutex     nested lock/unlock, then reacquire
 *   T4  counting semaphore  3 posts consumed; try_wait() on empty fails
 *   T5  binary semaphore    post/wait signal
 *   T6  event flags         raise/wait masked + auto-clear + try_wait
 *   T7  message queue       send/receive round-trip with payload verify
 *   T8  memory pool         alloc/exhaust/free/reuse + capacity invariant
 *   T9  condition variable  predicate wait + signal (canonical mutex pattern)
 *   T10 periodic timer      ~20 firings in ~205 ms
 *
 * Visualisation:
 *   - UART0 @115200: one "PASS/FAIL <name>" line per sub-test, then a final
 *     "RESULT: PASS/FAIL (p passed, f failed)".
 *   - LED (GPIO25): a short blink per passed sub-test (triple-blink on a
 *     failure); after the summary, a slow heartbeat = all PASS, fast blink =
 *     some FAIL.
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/led.hpp>
#include <bsp/uart.hpp>
#include <hw_result.hpp>

#include <cerrno>
#include <cstdint>
#include <cstdio>

using namespace os::rtos;

namespace
{
  // ---- Objects under test (file scope; constructed after the free store is
  // up via the constructor(101) early-init, so mq/mp allocations succeed). ----
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

  // ---- Cross-thread handshake + results -----------------------------------
  semaphore_counting sem_go{ "go", 8, 0 };   // controller -> helper
  semaphore_counting sem_ack{ "ack", 8, 0 }; // helper -> controller

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

  mutex g_log{ "log" }; // serialise UART writes
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
    int n = std::snprintf (b, sizeof (b), "[%8lu ms] %-4s %s\n",
                           static_cast<unsigned long> (sysclock.now ()),
                           ok ? "PASS" : "FAIL", name);
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

  // ---- Periodic-timer callback (runs in the tick context) -----------------
  void
  tmr_cb (timer::func_args_t)
  {
    ++g_timer_fires;
  }

  // ---- Helper thread: the second party for the hand-off tests -------------
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
            // mtx is held by the controller: try_lock must NOT succeed.
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
              ef.raise (1u << 2); // hand a flag back to the controller
            }
            break;

          case ST_MQ:
            {
              msg_t m{};
              g_helper_mq_ok = 0;
              if (mq_c2w.receive (&m, sizeof (m)) == result::ok)
                {
                  m.data ^= 0xFFFFu; // transform to prove processing
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

  // ---- Controller thread: drive every object, self-check ------------------
  void*
  controller (void*)
  {
    logs ("\n");
    logs ("=============================================\n");
    logs ("  Pico / RP2040B - micro-os-plus-iii\n");
    logs ("  sc-test-ko : kernel objects, single core\n");
    logs ("=============================================\n");

    // T1 — mutex: mutual exclusion. Both threads add COUNT_N under the lock.
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
    check (g_counter == 2 * COUNT_N, "mutex: mutual-exclusion counter");

    // T2 — mutex: try_lock() must report busy while another thread holds it.
    mtx.lock ();
    g_stage = ST_TRYLOCK;
    sem_go.post ();
    sem_ack.wait ();
    mtx.unlock ();
    check (g_helper_trylock_busy == 1, "mutex: try_lock busy when held");

    // T3 — recursive mutex: nested lock/unlock, then reacquire.
    {
      bool ok = (rmtx.lock () == result::ok);
      ok = ok && (rmtx.lock () == result::ok);   // recursive, no self-deadlock
      ok = ok && (rmtx.unlock () == result::ok); // depth 2 -> 1
      ok = ok && (rmtx.unlock () == result::ok); // depth 1 -> 0
      ok = ok && (rmtx.try_lock () == result::ok); // now free
      rmtx.unlock ();
      check (ok, "recursive mutex: nested lock + reacquire");
    }

    // T4 — counting semaphore: helper consumes 3 posts; empty try_wait fails.
    g_stage = ST_CSEM;
    sem_go.post ();
    sysclock.sleep_for (5); // let helper block on the empty semaphore
    csem.post ();
    csem.post ();
    csem.post ();
    sem_ack.wait ();
    check (g_helper_csem_got == 3 && csem.try_wait () != result::ok,
           "counting semaphore: 3 consumed + empty try_wait");

    // T5 — binary semaphore: post/wait signal.
    g_stage = ST_BSEM;
    sem_go.post ();
    sysclock.sleep_for (5);
    bsem.post ();
    sem_ack.wait ();
    check (g_helper_bsem_ok == 1, "binary semaphore: post/wait");

    // T6 — event flags: helper waits bit0 (auto-clear), we raise bit0|bit1,
    // helper hands bit2 back, we wait it; unset-bit try_wait fails.
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
      bool tw = (ef.try_wait ((1u << 5)) != result::ok); // never raised
      check (g_helper_ef_ok == 1 && w && tw,
             "event flags: raise/wait mask + try_wait");
    }

    // T7 — message queue: send a payload, helper transforms + echoes it back.
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

    // T8 — memory pool: alloc all, exhaust, free, reuse, capacity invariant.
    {
      void* b[4];
      bool ok = true;
      for (int i = 0; i < 4; ++i)
        {
          b[i] = mp.try_alloc ();
          ok = ok && (b[i] != nullptr);
        }
      ok = ok && (mp.try_alloc () == nullptr); // pool exhausted
      mp.free (b[0]);
      void* reuse = mp.try_alloc (); // freed slot is reusable
      ok = ok && (reuse != nullptr);
      mp.free (reuse);
      for (int i = 1; i < 4; ++i)
        {
          mp.free (b[i]);
        }
      ok = ok && (mp.capacity () == 4);
      check (ok, "memory pool: alloc/exhaust/free/reuse + capacity");
    }

    // T9 — condition variable: helper waits on predicate; we set + signal.
    {
      g_cv_pred = false;
      g_stage = ST_CV;
      sem_go.post ();
      sysclock.sleep_for (10); // ensure the helper is parked in cv.wait()
      cvm.lock ();
      g_cv_pred = true;
      cv.signal ();
      cvm.unlock ();
      sem_ack.wait ();
      check (g_helper_cv_ok == 1, "condition variable: predicate wait + signal");
    }

    // T10 — periodic timer: ~20 firings across ~205 ms at a 10 ms period.
    {
      g_timer_fires = 0;
      timer t{ "t", tmr_cb, nullptr, timer::periodic_initializer };
      t.start (10); // 10 ticks == 10 ms
      sysclock.sleep_for (205);
      t.stop ();
      unsigned f = g_timer_fires;
      check (f >= 15 && f <= 25, "periodic timer: ~20 fires in 205 ms");
    }

    // Release the helper thread.
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

    // QEMU runs end here, through the semihosting exit that carries the
    // verdict; on hardware this is a no-op and the LED loop below runs.
    if (g_fail == 0)
      {
        hw_result::ok ();
      }
    else
      {
        hw_result::fail ();
      }

    // LED verdict forever: slow heartbeat = PASS, fast blink = FAIL.
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
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();

  thread::attributes attr = thread::initializer;

  attr.th_stack_address = ctl_stack;
  attr.th_stack_size_bytes = sizeof (ctl_stack);
  static thread ctl{ "controller", controller, nullptr, attr };

  attr.th_stack_address = hlp_stack;
  attr.th_stack_size_bytes = sizeof (hlp_stack);
  static thread hlp{ "helper", helper, nullptr, attr };

  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
