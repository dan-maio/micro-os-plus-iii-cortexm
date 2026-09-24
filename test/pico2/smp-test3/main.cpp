/*
 * smp-test3 (Pico 2 / RP2350) — Phase 3+: cross-core IPI ping-pong.
 *
 * The pico2 SMP port's cross-core IPI channel is the SIO inter-core FIFO:
 *   port_smp_ipi(cpu) pushes a word; the RECEIVING core's SIO_IRQ_FIFO
 *   (external IRQ 25) drains the FIFO and pends that core's PendSV, so the
 *   receiver reschedules immediately. The kernel does NOT send IPIs by
 *   itself — a thread woken on the other core is only noticed on that core's
 *   next SysTick (up to 1 ms later) unless the poster raises the IPI.
 *
 * This test proves the IPI path end to end, ISR -> thread, on LED + UART
 * (no SPI): a "pinger" thread pinned to core 0 and a "ponger" pinned to
 * core 1 pass a semaphore ping-pong, and we measure the one-way wake
 * latency with the RP2350 TIMER0 64-bit microsecond counter.
 *
 *   pinger (c0): t_sent = us(); s_ping.post();  [port_smp_ipi(1)]
 *   ponger (c1): s_ping.wait(); lat = us() - t_sent;   (core0 -> core1)
 *                t_sent = us(); s_pong.post(); [port_smp_ipi(0)]
 *   pinger (c0): s_pong.wait(); lat = us() - t_sent;   (core1 -> core0)
 *
 * A/B on one run:
 *   phase A (20 rounds)  : the poster does NOT send the IPI; the wake is
 *                          delivered by the receiver's next SysTick — one-way
 *                          latencies up to ~1 ms.
 *   phase B (2000 rounds): the poster sends the IPI; the FIFO IRQ delivers
 *                          the wake in microseconds.
 *
 * RESULT = PASS when every phase-B one-way latency is < 500 us (none was
 * tick-delivered), i.e. the SIO-FIFO IPI is what wakes the threads.
 *
 * Output on UART0 @115200; the LED blinks once per round (core 0) and
 * becomes a 200 ms heartbeat after the summary.
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/led.hpp>
#include <bsp/multicore.hpp>
#include <bsp/rp2350.hpp>
#include <bsp/uart.hpp>
#include <cmsis_device.h>

#include <hw_result.hpp>

#include <cstdio>
#include <cstdint>

using namespace os::rtos;

extern "C"
{
  // The pico2 SMP port (src/rtos/os-core.cpp).
  void port_smp_secondary_start (void);
  unsigned port_core1_stack_top (void);
  unsigned port_cpu_id (void);
  void port_smp_core0_ipi_enable (void);
  // Cross-core IPI: wake `cpu` over the SIO inter-core FIFO.
  void port_smp_ipi (unsigned cpu);
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
  // ---- RP2350 TIMER0: free-running 64-bit microsecond counter ------------
  // Two RP2350 quirks (unlike RP2040):
  //   1. The peripheral map moved: TIMER0 is at 0x400B0000.
  //   2. Most peripherals BOOT HELD IN RESET (RESET defaults to 0x1FFFFFFF),
  //      so TIMER0 (bit 23) must be released via RESET/RESET_DONE, exactly
  //      the pico-sdk unreset_block_wait().
  //   3. The counter only ticks when its tick generator runs. The RP2350
  //      TICKS block (0x40108000) drives TIMER0 from clk_ref; it must be
  //      started with cycles = clk_ref/1MHz (clk_ref = 12 MHz XOSC -> 12)
  //      and ENABLE set — the pico-sdk start_all_ticks() does this AFTER
  //      configuring the clocks, which our BSP clocks.cpp (and the bootrom
  //      re-configuring clk_ref) leaves off.
  //
  // Reading TIMEHR latches the 64-bit pair, so TIMEHR then TIMELR is an
  // atomic snapshot. All deltas used here fit in 32 bits.
  inline constexpr std::uint32_t TIMER0_BASE = 0x400B0000U;
  inline constexpr std::uint32_t TIMER0_RESET_BIT = 23U;
  inline constexpr std::uint32_t RESET_REG = rp2350::RESETS_BASE + 0x00U;
  inline constexpr std::uint32_t RESET_DONE_REG = rp2350::RESETS_BASE + 0x08U;
  inline constexpr std::uint32_t TIMER_TIMEHR = TIMER0_BASE + 0x08U;
  inline constexpr std::uint32_t TIMER_TIMELR = TIMER0_BASE + 0x0CU;

  inline constexpr std::uint32_t TICKS_BASE = 0x40108000U;
  inline constexpr std::uint32_t TICKS_TIMER0_CTRL = TICKS_BASE + 0x18U;
  inline constexpr std::uint32_t TICKS_TIMER0_CYCLES = TICKS_BASE + 0x1CU;
  inline constexpr std::uint32_t TICKS_CTRL_ENABLE = 1U << 0;
  inline constexpr std::uint32_t CLK_REF_HZ = 12000000U; // XOSC, as clocks.cpp

  void
  timer_init (void)
  {
    rp2350::reg (RESET_REG) &= ~(1U << TIMER0_RESET_BIT);
    while (!(rp2350::reg (RESET_DONE_REG) & (1U << TIMER0_RESET_BIT)))
      {
      }
    rp2350::reg (TICKS_TIMER0_CYCLES) = CLK_REF_HZ / 1000000U;
    rp2350::reg (TICKS_TIMER0_CTRL) = TICKS_CTRL_ENABLE;
  }

  inline std::uint32_t
  timer_us32 (void)
  {
    std::uint32_t hi = rp2350::reg (TIMER_TIMEHR);
    std::uint32_t lo = rp2350::reg (TIMER_TIMELR);
    (void) hi;
    return lo;
  }

  // ---- Test parameters ----------------------------------------------------
  constexpr std::uint32_t PH_A_ROUNDS = 20;   // no-IPI control
  constexpr std::uint32_t PH_B_ROUNDS = 2000; // IPI
  constexpr std::uint32_t SLOW_US = 500;      // >= this => tick-delivered

  // ---- Cross-core handshake ----------------------------------------------
  semaphore_counting s_ping{ "ping", 8, 0 };
  semaphore_counting s_pong{ "pong", 8, 0 };

  // Sender's departure time (us), read by the receiver right after the wake.
  // Written by one core, read by the other; __DMB() brackets the exchange so
  // the IRQ/FIFO ordering makes the value visible before the wake is posted.
  volatile std::uint32_t g_t_sent = 0;

  // Which phase we are in (0 = A, 1 = B, 2 = done). Set by the pinger only,
  // read each round by both; the strict ping->pong alternation serializes it.
  volatile unsigned g_phase = 0;
  std::uint32_t g_round = 0; // pinger only

  // One-way latency statistics. Each core accumulates ONLY its own leg
  // (single writer); the pinger reads both after the last pong completes.
  struct stats
  {
    volatile std::uint32_t min;
    volatile std::uint32_t sum;
    volatile std::uint32_t max;
    volatile std::uint32_t cnt;
    volatile std::uint32_t slow;
  };
  stats g_leg_c1[2]; // core0 -> core1, written by the ponger (core 1)
  stats g_leg_c0[2]; // core1 -> core0, written by the pinger (core 0)

  void
  acc (stats& st, std::uint32_t lat)
  {
    if (lat < st.min)
      {
        st.min = lat;
      }
    st.sum = st.sum + lat;
    if (lat > st.max)
      {
        st.max = lat;
      }
    st.cnt = st.cnt + 1;
    if (lat >= SLOW_US)
      {
        st.slow = st.slow + 1;
      }
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

  void
  line (const char* s)
  {
    uart::write (s);
  }

  void
  print_stats (const char* leg, const stats& st)
  {
    char b[128];
    int n = std::snprintf (
        b, sizeof (b), "  %s one-way: min=%-4u avg=%-4u max=%-4u us "
                       "(slow>=%u: %u/%u)\n",
        leg, static_cast<unsigned> (st.min),
        static_cast<unsigned> (st.cnt ? st.sum / st.cnt : 0),
        static_cast<unsigned> (st.max), SLOW_US,
        static_cast<unsigned> (st.slow), static_cast<unsigned> (st.cnt));
    uart::write (b, static_cast<std::size_t> (n));
  }

  bool
  report (void)
  {
    char b[256];
    int n = std::snprintf (
        b, sizeof (b),
        "-------------------------------------------------\n"
        "cross-core IPI ping-pong : results (LED+UART, no SPI)\n"
        "  phase A: tick-only wake (no IPI)   %u rounds\n"
        "  phase B: SIO-FIFO IPI wake         %u rounds\n"
        "-------------------------------------------------\n",
        PH_A_ROUNDS, PH_B_ROUNDS);
    uart::write (b, static_cast<std::size_t> (n));

    print_stats ("A core0->1", g_leg_c1[0]);
    print_stats ("A core1->0", g_leg_c0[0]);
    print_stats ("B core0->1", g_leg_c1[1]);
    print_stats ("B core1->0", g_leg_c0[1]);

    bool pass = (g_leg_c1[1].max < SLOW_US) && (g_leg_c0[1].max < SLOW_US)
                && (g_leg_c1[1].cnt == PH_B_ROUNDS)
                && (g_leg_c0[1].cnt == PH_B_ROUNDS);
    n = std::snprintf (
        b, sizeof (b), "IPIs sent: %u (all phase B). "
                       "Result: %s\n",
        PH_B_ROUNDS * 2U, pass ? "PASS (IPI wake proven)" : "FAIL");
    uart::write (b, static_cast<std::size_t> (n));
    return pass;
  }

  // ---- core-1 ponger ------------------------------------------------------
  void*
  ponger (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 1);
    if (port_cpu_id () != 1)
      {
        this_thread::yield ();
      }
    for (;;)
      {
        s_ping.wait ();
        std::uint32_t lat = timer_us32 () - g_t_sent;
        unsigned ph = g_phase;
        acc (g_leg_c1[ph], lat);
        g_t_sent = timer_us32 ();
        __DMB ();
        s_pong.post ();
        if (ph == 1)
          {
            port_smp_ipi (0); // wake core 0 immediately
          }
      }
    return nullptr;
  }

  // ---- core-0 pinger ------------------------------------------------------
  void*
  pinger (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 0);
    if (port_cpu_id () != 0)
      {
        this_thread::yield ();
      }
    g_leg_c1[0].min = ~0U;
    g_leg_c1[1].min = ~0U;
    g_leg_c0[0].min = ~0U;
    g_leg_c0[1].min = ~0U;
    for (;;)
      {
        unsigned ph = g_phase;

        g_t_sent = timer_us32 ();
        __DMB ();
        s_ping.post ();
        if (ph == 1)
          {
            port_smp_ipi (1); // wake core 1 immediately
          }

        s_pong.wait ();
        std::uint32_t lat = timer_us32 () - g_t_sent;
        acc (g_leg_c0[ph], lat);

        led_toggle ();
        g_round = g_round + 1;

        if (ph == 0 && g_round >= PH_A_ROUNDS)
          {
            g_phase = 1;
            g_round = 0;
          }
        else if (ph == 1 && g_round >= PH_B_ROUNDS)
          {
            g_phase = 2; // stop: the ponger idles on s_ping
            const bool ok = report ();
            {
              char ab[64];
              int an = std::snprintf (ab, sizeof (ab), "RESULT: %s\n",
                                      ok ? "PASS" : "FAIL");
              uart::write (ab, static_cast<std::size_t> (an));
            }
            // Ends through the strong _Exit() -> semihosting SYS_EXIT;
            // hw_result also reports RESULT on the runner's console.
            if (ok)
              {
                hw_result::ok ();
              }
            else
              {
                hw_result::fail ();
              }
            // Slow heartbeat: the board is alive, measurement done.
            for (;;)
              {
                sysclock.sleep_for (200);
                led_toggle ();
              }
          }
      }
    return nullptr;
  }

  // Core-1 idle thread (as in smp-test2).
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
  timer_init (); // TIMER0 is held in reset at boot

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico 2 / RP2350 - micro-os-plus-iii\n");
  uart::write ("  smp-test3 : cross-core IPI ping-pong\n");
  uart::write ("=============================================\n");

  // Self-check the microsecond timer is running (a wrong base would read
  // garbage and every latency would be meaningless).
  {
    std::uint32_t t0 = timer_us32 ();
    volatile std::uint32_t spin = 0;
    while (spin < 150000)
      {
        spin = spin + 1;
      }
    std::uint32_t t1 = timer_us32 ();
    char b[96];
    int n = std::snprintf (b, sizeof (b), "timer self-check: %u us elapsed\n",
                           t1 - t0);
    uart::write (b, static_cast<std::size_t> (n));
  }

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

  uart::write ("core 0: idle1 installed; creating pinger/ponger...\n");

  thread::attributes attr = thread::initializer;

  static thread::stack::element_t pinger_stack[1024];
  static thread::stack::element_t ponger_stack[1024];
  attr.th_stack_address = pinger_stack;
  attr.th_stack_size_bytes = sizeof (pinger_stack);
  static thread pinger_th{ "pinger", pinger, nullptr, attr };
  pinger_th.cpu_affinity (1u << 0);

  attr.th_stack_address = ponger_stack;
  attr.th_stack_size_bytes = sizeof (ponger_stack);
  static thread ponger_th{ "ponger", ponger, nullptr, attr };
  ponger_th.cpu_affinity (1u << 1);

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
