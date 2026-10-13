/*
 * smp-test4 (Pico / RP2040) — Phase 4+: kernel objects under dual-core
 * contention.
 *
 * smp-test3 proved the cross-core IPI (SIO-FIFO wake in ~13 us). This test
 * proves the RTOS KERNEL OBJECTS work across both cores, using the same
 * two-core setup (pinger/worker pair on core 0 and core 1), LED + UART only
 * (no SPI). No explicit IPI is used here: cross-core wakeups ride the normal
 * tick-delivered ready-list path, exactly like a plain µOS++ app.
 *
 * A "controller" thread (core 0) runs seven sub-tests sequentially; a
 * "worker" thread (core 1) participates in the cross-core ones. Each sub-test
 * is a strict round-trip through the object under test, so it self-checks:
 *
 *   S1 event_flags   — raise/wait ping-pong across cores (level-based, so a
 *                      raise before a wait cannot be lost).
 *   S2 message_queue — send/receive ping-pong across cores, payload verify.
 *   S3 memory_pool   — alloc on core 0, free on core 1, count invariant.
 *   S4 condvar       — canonical mutex+predicate pattern across cores
 *                      (signal can never be lost).
 *   S5 mutex (PI)    — worker blocks in lock() while core 0 holds the mutex
 *                      (cross-core blocking + priority-inheritance path),
 *                      then acquires it and ack round-trips.
 *   S6 timeouts      — semaphore/event-flags timed_wait return ETIMEDOUT
 *                      after ~the requested duration (clock path).
 *   S7 timer         — periodic 10 ms timer on core 0, ~100 firings/1000 ms.
 *
 * A "worker armed" handshake plus strict per-round alternation makes every
 * stage free of lost wakeups. The LED blinks once per controller round and
 * becomes a heartbeat after the summary. Output on UART0 @115200.
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
  // ---- RP2040 TIMER: free-running 64-bit microsecond counter -------------
  // The TIMER is at 0x40054000, boots held in reset (RESETS bit 21), and
  // counts the watchdog tick, which must run at 1 us (WATCHDOG_TICK = 12
  // clk_ref cycles of the 12 MHz XOSC | ENABLE). The board's clocks::init()
  // already does both; doing them again keeps the test self-contained, as it
  // was on the Pico 2. TIMERAWL is the raw (unlatched) low word: all deltas
  // used here fit in 32 bits.
  void
  timer_init (void)
  {
    rp2040::reg (rp2040::RESETS_BASE + 0x00U) &= ~rp2040::RESET_TIMER;
    while (!(rp2040::reg (rp2040::RESETS_BASE + 0x08U) & rp2040::RESET_TIMER))
      {
      }
    rp2040::reg (rp2040::WATCHDOG_TICK) = (1U << 9) | 12U;
  }

  inline std::uint32_t
  timer_us32 (void)
  {
    return rp2040::reg (rp2040::TIMER_TIMERAWL);
  }

  // ---- Test parameters ----------------------------------------------------
  constexpr std::uint32_t EV_R = 200;
  constexpr std::uint32_t MQ_R = 200;
  constexpr std::uint32_t MP_R = 200;
  constexpr std::uint32_t CV_R = 200;
  constexpr std::uint32_t PI_R = 100;
  constexpr std::uint32_t TMO_R = 30;
  constexpr std::uint32_t TIMEOUT_TICKS = 10; // 10 ms
  constexpr std::uint32_t TMR_PERIOD_TICKS = 10; // 10 ms
  constexpr std::uint32_t TMR_RUN_TICKS = 1000; // ~100 firings

  struct msg_t
  {
    std::uint32_t seq;
    std::uint32_t tag;
  };

  // ---- Shared objects -----------------------------------------------------
  event_flags ef{ "ef" };
  event_flags ef2{ "ef2" };
  // S2 uses two queues (controller->worker / worker->controller): a single
  // shared queue lets the controller's own send/receive pair self-serve its
  // message before the (tick-deferred) worker receive sees it.
  message_queue mq_c2w{ "c2w", 16, sizeof (msg_t) };
  message_queue mq_w2c{ "w2c", 16, sizeof (msg_t) };
  memory_pool mp{ "mp", 8, sizeof (msg_t) };
  mutex m_cv{ "cv_m" };
  condition_variable cv{ "cv" };
  mutex m_pi{ "pi" };
  semaphore_counting sem_armed{ "armed", 4, 0 };
  semaphore_counting sem_go{ "go", 8, 0 };
  semaphore_counting sem_ack{ "ack", 8, 0 };
  semaphore_counting sem_t{ "tmo", 8, 0 };

  // Controller <-> worker coordination (volatile, cross-core).
  volatile unsigned g_stage = 0;   // 0 = idle, 1..5 = worker stages
  volatile std::uint32_t g_wdone = 0;   // worker finished stage N
  volatile bool g_pred = false;         // condvar predicate
  volatile void* g_mp_block = nullptr;  // pool block handed to the worker

  // Worker-side results (volatile; written by core 1, read after g_wdone).
  volatile std::uint32_t g_werr = 0;

  // Periodic-timer callback (runs in the system-tick context on core 0).
  volatile std::uint32_t g_tmr_cnt = 0;
  void
  tmr_cb (void*)
  {
    g_tmr_cnt = g_tmr_cnt + 1;
  }

  // ---- Console (controller thread only) ----------------------------------
  void
  line (const char* s)
  {
    uart::write (s);
  }

  void
  led_toggle (void)
  {
    static bool on = false;
    on = !on;
    if (on)
      {
        led::on ();
      }
    else
      {
        led::off ();
      }
  }

  // ---- Core-1 worker ------------------------------------------------------
  // Each stage runs exactly N rounds (the same N the controller runs — the
  // strict per-round alternation keeps both sides in lockstep), then signals
  // g_wdone and SPINS until the controller advances g_stage, so it never
  // re-enters a finished stage.
  void*
  worker (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 1);
    if (port_cpu_id () != 1)
      {
        this_thread::yield ();
      }
    for (;;)
      {
        unsigned st = g_stage;
        std::uint32_t r;
        switch (st)
          {
          case 1: // S1 event_flags ping-pong
            sem_armed.post ();
            g_werr = 0;
            r = 0;
            while (g_stage == 1 && r < EV_R)
              {
                if (ef.wait (1u << 0) != result::ok)
                  {
                    g_werr = g_werr + 1;
                  }
                ef.raise (1u << 1);
                r = r + 1;
              }
            g_wdone = st;
            while (g_stage == 1)
              {
              }
            break;
          case 2: // S2 message_queue ping-pong (two queues)
            sem_armed.post ();
            g_werr = 0;
            r = 0;
            while (g_stage == 2 && r < MQ_R)
              {
                msg_t m{ 0, 0 };
                if (mq_c2w.receive (&m, sizeof (m)) != result::ok)
                  {
                    g_werr = g_werr + 1;
                  }
                else if (m.tag != 0xCAFEu)
                  {
                    g_werr = g_werr + 1;
                  }
                else
                  {
                    m.tag = 0xBEEFu;
                  }
                // Always echo back, so the round-trip cannot break.
                if (mq_w2c.send (&m, sizeof (m)) != result::ok)
                  {
                    g_werr = g_werr + 1;
                  }
                r = r + 1;
              }
            g_wdone = st;
            while (g_stage == 2)
              {
              }
            break;
          case 3: // S3 memory_pool: free a block allocated on core 0
            sem_armed.post ();
            g_werr = 0;
            r = 0;
            while (g_stage == 3 && r < MP_R)
              {
                sem_go.wait ();
                auto* b = static_cast<msg_t*> (const_cast<void*> (g_mp_block));
                if (b->tag != 0x0DDDu)
                  {
                    g_werr = g_werr + 1;
                  }
                mp.free (b);
                sem_ack.post ();
                r = r + 1;
              }
            g_wdone = st;
            while (g_stage == 3)
              {
              }
            break;
          case 4: // S4 condition_variable (canonical mutex+predicate)
            sem_armed.post ();
            g_werr = 0;
            r = 0;
            while (g_stage == 4 && r < CV_R)
              {
                m_cv.lock ();
                while (!g_pred)
                  {
                    cv.wait (m_cv);
                  }
                g_pred = false;
                m_cv.unlock ();
                sem_ack.post ();
                r = r + 1;
              }
            g_wdone = st;
            while (g_stage == 4)
              {
              }
            break;
          case 5: // S5 mutex: block across cores while core 0 holds it
            sem_armed.post ();
            g_werr = 0;
            r = 0;
            while (g_stage == 5 && r < PI_R)
              {
                sem_go.wait ();
                m_pi.lock (); // blocks until core 0 releases (PI path)
                m_pi.unlock ();
                sem_ack.post ();
                r = r + 1;
              }
            g_wdone = st;
            while (g_stage == 5)
              {
              }
            break;
          default: // idle while the controller runs S6/S7
            sysclock.sleep_for (1);
            break;
          }
      }
    return nullptr;
  }

  // ---- Core-0 controller --------------------------------------------------
  void*
  controller (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 0);
    if (port_cpu_id () != 0)
      {
        this_thread::yield ();
      }

    bool all_pass = true;
    std::uint32_t err;
    char b[160];

    // ---- S1: event_flags -------------------------------------------------
    bool ok = true;
    g_stage = 1;
    sem_armed.wait ();
    for (std::uint32_t r = 0; r < EV_R; ++r)
      {
        ef.raise (1u << 0);
        if (ef.wait (1u << 1) != result::ok)
          {
            ok = false;
          }
        led_toggle ();
      }
    while (g_wdone != 1)
      {
      }
    err = g_werr;
    all_pass = all_pass && ok && (err == 0);
    std::snprintf (b, sizeof (b),
                   "S1 event_flags   : %s (%u rounds, worker err %u)\n",
                   (ok && err == 0) ? "PASS" : "FAIL", EV_R, err);
    uart::write (b);

    // ---- S2: message_queue (two queues, no self-serve) --------------------
    ok = true;
    g_stage = 2;
    sem_armed.wait ();
    for (std::uint32_t r = 0; r < MQ_R; ++r)
      {
        msg_t m{ r, 0xCAFEu };
        if (mq_c2w.send (&m, sizeof (m)) != result::ok)
          {
            ok = false;
          }
        msg_t back;
        if (mq_w2c.receive (&back, sizeof (back)) != result::ok)
          {
            ok = false;
          }
        else if (back.seq != r || back.tag != 0xBEEFu)
          {
            ok = false;
          }
        led_toggle ();
      }
    while (g_wdone != 2)
      {
      }
    err = g_werr;
    all_pass = all_pass && ok && (err == 0);
    std::snprintf (b, sizeof (b),
                   "S2 message_queue : %s (%u rounds, worker err %u)\n",
                   (ok && err == 0) ? "PASS" : "FAIL", MQ_R, err);
    uart::write (b);

    // ---- S3: memory_pool (alloc core 0, free core 1) ----------------------
    ok = true;
    g_stage = 3;
    sem_armed.wait ();
    for (std::uint32_t r = 0; r < MP_R; ++r)
      {
        auto* b = static_cast<msg_t*> (mp.alloc ());
        b->seq = r;
        b->tag = 0x0DDDu;
        g_mp_block = b;
        sem_go.post ();
        sem_ack.wait ();
        // count() = ALLOCATED blocks; the worker freed it -> must be 0.
        if (mp.count () != 0)
          {
            ok = false;
          }
        led_toggle ();
      }
    while (g_wdone != 3)
      {
      }
    err = g_werr;
    all_pass = all_pass && ok && (err == 0);
    std::snprintf (b, sizeof (b),
                   "S3 memory_pool   : %s (%u rounds, worker err %u)\n",
                   (ok && err == 0) ? "PASS" : "FAIL", MP_R, err);
    uart::write (b);

    // ---- S4: condition_variable ------------------------------------------
    ok = true;
    g_stage = 4;
    sem_armed.wait ();
    for (std::uint32_t r = 0; r < CV_R; ++r)
      {
        m_cv.lock ();
        g_pred = true;
        cv.signal ();
        m_cv.unlock ();
        if (sem_ack.wait () != result::ok)
          {
            ok = false;
          }
        led_toggle ();
      }
    while (g_wdone != 4)
      {
      }
    err = g_werr;
    all_pass = all_pass && ok && (err == 0);
    std::snprintf (b, sizeof (b),
                   "S4 condvar       : %s (%u rounds, worker err %u)\n",
                   (ok && err == 0) ? "PASS" : "FAIL", CV_R, err);
    uart::write (b);

    // ---- S5: mutex (cross-core blocking + priority inheritance) ----------
    ok = true;
    g_stage = 5;
    sem_armed.wait ();
    for (std::uint32_t r = 0; r < PI_R; ++r)
      {
        m_pi.lock ();
        sem_go.post ();      // worker now blocks in lock()...
        sysclock.sleep_for (5); // ...while core 0 holds the mutex
        m_pi.unlock ();
        if (sem_ack.wait () != result::ok)
          {
            ok = false;
          }
        led_toggle ();
      }
    while (g_wdone != 5)
      {
      }
    err = g_werr;
    all_pass = all_pass && ok && (err == 0);
    std::snprintf (b, sizeof (b),
                   "S5 mutex (PI)    : %s (%u rounds, worker err %u)\n",
                   (ok && err == 0) ? "PASS" : "FAIL", PI_R, err);
    uart::write (b);

    // ---- S6: timeouts (controller only) ----------------------------------
    g_stage = 6; // worker idles
    std::uint32_t tmo_ok = 0, tmo_bad = 0;
    std::uint32_t tmo_sum = 0;
    for (std::uint32_t r = 0; r < TMO_R; ++r)
      {
        std::uint32_t t0 = timer_us32 ();
        result_t res = sem_t.timed_wait (TIMEOUT_TICKS);
        std::uint32_t dt = timer_us32 () - t0;
        if (res == ETIMEDOUT && dt >= 9 * 1000 && dt <= 30 * 1000)
          {
            tmo_ok = tmo_ok + 1;
            tmo_sum = tmo_sum + dt;
          }
        else
          {
            tmo_bad = tmo_bad + 1;
          }
        led_toggle ();
      }
    // Wake-before-timeout control: post then wait must return ok, fast.
    std::uint32_t fast_bad = 0;
    for (std::uint32_t r = 0; r < 20; ++r)
      {
        std::uint32_t t0 = timer_us32 ();
        sem_t.post ();
        result_t res = sem_t.timed_wait (100);
        std::uint32_t dt = timer_us32 () - t0;
        if (res != result::ok || dt > 5000)
          {
            fast_bad = fast_bad + 1;
          }
        led_toggle ();
      }
    bool tmo_pass = (tmo_bad == 0 && fast_bad == 0);
    all_pass = all_pass && tmo_pass;
    std::snprintf (b, sizeof (b),
                   "S6 timeouts      : %s (%u ETIMEDOUT, bad %u; "
                   "fast-wake bad %u; dt avg %u us)\n",
                   tmo_pass ? "PASS" : "FAIL", tmo_ok, tmo_bad, fast_bad,
                   tmo_ok ? tmo_sum / tmo_ok : 0);
    uart::write (b);

    // ---- S7: periodic timer (controller only) ----------------------------
    g_stage = 7; // worker idles
    g_tmr_cnt = 0;
    {
      timer t{ "t", tmr_cb, nullptr, timer::periodic_initializer };
      t.start (TMR_PERIOD_TICKS);
      sysclock.sleep_for (TMR_RUN_TICKS);
      t.stop ();
    }
    std::uint32_t cnt = g_tmr_cnt;
    bool tmr_pass = (cnt >= 90 && cnt <= 115);
    all_pass = all_pass && tmr_pass;
    std::snprintf (b, sizeof (b),
                   "S7 timer         : %s (count %u in ~%u ms, expect ~100)\n",
                   tmr_pass ? "PASS" : "FAIL", cnt,
                   TMR_RUN_TICKS * 1u);
    uart::write (b);

    // ---- Summary ----------------------------------------------------------
    line ("---------------------------------------------\n");
    std::snprintf (b, sizeof (b), "RESULT: %s\n",
                   all_pass ? "PASS (kernel objects under dual-core contention)"
                            : "FAIL");
    uart::write (b);

    // End the run with the verdict (semihosting exit).
    if (all_pass)
      {
        hw_result::ok ();
      }
    else
      {
        hw_result::fail ();
      }

    for (;;)
      {
        sysclock.sleep_for (200);
        led_toggle ();
      }
    return nullptr;
  }

  // Core-1 idle thread (as in smp-test2/3).
  void*
  secondary_idle_func (void*)
  {
    this_thread::thread ().priority (thread::priority::idle);
    while (true)
      {
        __asm__ volatile ("dsb" ::: "memory");
        __asm__ volatile ("wfi");
        this_thread::yield ();
      }
    return nullptr;
  }
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  this_thread::thread ().cpu_affinity (1u << 0);

  led::init ();
  timer_init ();

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico / RP2040 - micro-os-plus-iii\n");
  uart::write ("  smp-test4 : kernel objects, dual-core\n");
  uart::write ("=============================================\n");

  static thread::stack::element_t idle1_stack[512];
  static thread* idle1_thread_ptr;
  {
    thread::attributes attr = thread::initializer;
    attr.th_stack_address = idle1_stack;
    attr.th_stack_size_bytes = sizeof (idle1_stack);
    idle1_thread_ptr = new thread ("idle1", secondary_idle_func, nullptr,
                                   attr);
    os::rtos::scheduler::os_idle_thread_core[1] = idle1_thread_ptr;
  }

  uart::write ("core 0: idle1 installed; creating controller/worker...\n");

  thread::attributes attr = thread::initializer;

  static thread::stack::element_t ctl_stack[1024];
  static thread::stack::element_t wrk_stack[1024];
  attr.th_stack_address = ctl_stack;
  attr.th_stack_size_bytes = sizeof (ctl_stack);
  static thread controller_th{ "controller", controller, nullptr, attr };
  controller_th.cpu_affinity (1u << 0);

  attr.th_stack_address = wrk_stack;
  attr.th_stack_size_bytes = sizeof (wrk_stack);
  static thread worker_th{ "worker", worker, nullptr, attr };
  worker_th.cpu_affinity (1u << 1);

  uart::write ("core 0: launching core 1 (bootrom FIFO handshake)...\n");
  multicore::launch_core1 (port_smp_secondary_start,
                           port_core1_stack_top ());
  port_smp_core0_ipi_enable ();
  uart::write ("core 0: core 1 launched; SMP running.\n");

  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
