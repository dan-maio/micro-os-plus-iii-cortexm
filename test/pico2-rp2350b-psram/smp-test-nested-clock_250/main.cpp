/*
 * smp-test-nested (Pico 2 / RP2350B) — Nested Interrupts & SMP.
 *
 * Demonstrates:
 *   1. Multicore scheduler SMP with thread migration (no affinity pins for workers/LED).
 *   2. Nested Interrupts on Core 0 and Core 1:
 *      - SysTick (priority 15 - lowest) driving scheduling and sleep timers.
 *      - UART0_IRQ_Handler (priority 12 - medium) triggered manually.
 *      - TIMER0_IRQ_2_Handler (priority 8 - highest) firing every 1.25 s.
 *      - TIMER0_IRQ_3_Handler (priority 10 - medium-high) firing every 1.2495 s.
 *   3. Lock-free real-time trace logging with RAM-placed time-critical routines
 *      to optimize interrupt latency and eliminate cache-miss wait states.
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/adc.hpp>
#include <bsp/clocks.hpp>
#include <bsp/led.hpp>
#include <bsp/multicore.hpp>
#include <bsp/rp2350.hpp>
#include <bsp/uart.hpp>
#include <hw_result.hpp>
#include <cmsis_device.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace os::rtos;

extern "C"
{
  void port_smp_secondary_start (void);
  unsigned port_core1_stack_top (void);
  unsigned port_cpu_id (void);
  void port_smp_core0_ipi_enable (void);
  
  // Interrupt handlers mapped in the vector table
  void UART0_IRQ_Handler (void) __attribute__((section(".data"), noinline));
  void TIMER0_IRQ_2_Handler (void) __attribute__((section(".data"), noinline));
  void TIMER0_IRQ_3_Handler (void) __attribute__((section(".data"), noinline));
}

namespace os
{
  namespace rtos
  {
    namespace scheduler
    {
      extern thread* os_idle_thread_core[OS_NCPU];
      extern thread* volatile current_thread_[OS_NCPU];
    }
  }
}

namespace
{
  unsigned g_global = 0;
  mutex g_mutex{ "g" };

  thread::stack::element_t worker_stacks[2][1024];
  thread::stack::element_t led_stack[1024];
  thread::stack::element_t trigger_stack[1024];
  thread::stack::element_t trace_stack[1024];
  thread::stack::element_t pot_stack[1024];

  struct ctx
  {
    int id;
    unsigned local;
  };
  ctx worker_ctx[2];

  // How many times each nesting interrupt ran, for the verdict.
  volatile unsigned g_irq_uart = 0;
  volatile unsigned g_irq_t2 = 0;
  volatile unsigned g_irq_t3 = 0;

  // ---- TIMER0 Alarm definitions -------------------------------------------
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
  inline constexpr std::uint32_t CLK_REF_HZ = 12000000U;

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

  std::uint32_t
  timer_us32 (void) __attribute__((section(".data"), noinline));

  std::uint32_t
  timer_us32 (void)
  {
    std::uint32_t hi = rp2350::reg (TIMER_TIMEHR);
    std::uint32_t lo = rp2350::reg (TIMER_TIMELR);
    (void) hi;
    return lo;
  }

  void
  alarm2_arm (std::uint32_t delay_us) __attribute__((section(".data"), noinline));

  void
  alarm2_arm (std::uint32_t delay_us)
  {
    std::uint32_t now = timer_us32 ();
    rp2350::reg (TIMER0_BASE + 0x18) = now + delay_us; // ALARM2 register
  }

  void
  alarm3_arm (std::uint32_t delay_us) __attribute__((section(".data"), noinline));

  void
  alarm3_arm (std::uint32_t delay_us)
  {
    std::uint32_t now = timer_us32 ();
    rp2350::reg (TIMER0_BASE + 0x1c) = now + delay_us; // ALARM3 register
  }

  // ---- Lock-Free Trace Logging ---------------------------------------------
  struct trace_event_t
  {
    std::uint32_t timestamp;
    std::uint8_t core;
    const char* event_name;
    const char* thread_name;
    void* thread_id;
  };

  constexpr std::size_t TRACE_MAX = 512;
  trace_event_t g_trace_buffer[TRACE_MAX];
  volatile std::uint32_t g_trace_count = 0;

  // SIO Spinlock 4 protects the trace buffer counter increment.
  // Interrupts are disabled locally on the core before acquiring the spinlock
  // to prevent same-core preemption deadlock.
  constexpr std::uint32_t TRACE_LOCK = rp2350::SIO_SPINLOCK0 + 16U; // slot 4

  std::uint32_t
  allocate_trace_slot (void) __attribute__((section(".data"), noinline));

  std::uint32_t
  allocate_trace_slot (void)
  {
    std::uint32_t saved = __get_PRIMASK ();
    __asm__ volatile ("cpsid i" ::: "memory");
    while (rp2350::reg (TRACE_LOCK) == 0)
      {
      }
    std::uint32_t idx = g_trace_count++;
    rp2350::reg (TRACE_LOCK) = 1;
    __set_PRIMASK (saved);
    return idx;
  }

  void
  log_trace_at (const char* event_name, std::uint32_t timestamp) __attribute__((section(".data"), noinline));

  void
  log_trace_at (const char* event_name, std::uint32_t timestamp)
  {
    std::uint32_t idx = allocate_trace_slot ();
    if (idx < TRACE_MAX)
      {
        unsigned cpu = port_cpu_id ();
        g_trace_buffer[idx] = {
          timestamp,
          static_cast<std::uint8_t> (cpu),
          event_name,
          os::rtos::scheduler::current_thread_[cpu] ?
            os::rtos::scheduler::current_thread_[cpu]->name () : "none",
          static_cast<void*> (os::rtos::scheduler::current_thread_[cpu])
        };
      }
  }

  void
  log_trace (const char* event_name) __attribute__((section(".data"), noinline));

  void
  log_trace (const char* event_name)
  {
    log_trace_at (event_name, timer_us32 ());
  }

  // ---- Worker/Heartbeat line prints ----------------------------------------
  void
  line (const char* role, int id, unsigned local, unsigned global)
  {
    char b[128];
    int n = std::snprintf (
        b, sizeof (b), "[%8lu ms] %-6s #%d (ID: %p) core=%u local=%-4u global=%u\n",
        static_cast<unsigned long> (sysclock.now ()), role, id,
        static_cast<void*> (&this_thread::thread ()), port_cpu_id (), local, global);
    g_mutex.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_mutex.unlock ();
  }

  // ---- Threads ------------------------------------------------------------
  void*
  worker (void* arg)
  {
    auto* c = static_cast<ctx*> (arg);
    for (;;)
      {
        c->local++;
        g_mutex.lock ();
        unsigned g = ++g_global;
        g_mutex.unlock ();
        line ("worker", c->id, c->local, g);
        log_trace ("Worker_Run");
        sysclock.sleep_for (1000);
      }
    return nullptr;
  }

  void*
  led_thread (void*)
  {
    for (;;)
      {
        sysclock.sleep_for (3000);
        line ("LED", 0, 0, g_global);
        log_trace ("LED_Blink");
        for (int i = 0; i < 3; ++i)
          {
            led::on ();
            sysclock.sleep_for (200);
            led::off ();
            sysclock.sleep_for (200);
          }
      }
    return nullptr;
  }

  void*
  trigger_thread (void*)
  {
    sysclock.sleep_for (2000);
    for (;;)
      {
        sysclock.sleep_for (1500); // Trigger nesting sequence every 1.5 seconds
        
        std::uint32_t trig_time = timer_us32 ();
        
        // Force pending of the UART0 exception (priority 12)
        NVIC_SetPendingIRQ (UART0_IRQn);
        
        log_trace_at ("Trigger", trig_time);
      }
    return nullptr;
  }

  void*
  trace_printer_thread (void*)
  {
    sysclock.sleep_for (3000); // Offset to avoid overlapping startup prints
    for (;;)
      {
        sysclock.sleep_for (4000); // Dump trace logs every 4 seconds
        
        g_mutex.lock ();
        uart::write ("\n================== TRACE LOG BUFFER DUMP ==================\n");
        
        std::uint32_t count = g_trace_count;
        if (count > TRACE_MAX)
          {
            count = TRACE_MAX;
          }
        
        for (std::uint32_t i = 0; i < count; ++i)
          {
            char buf[128];
            int len = std::snprintf (buf, sizeof (buf),
              "[%8lu us] C%u Event: %-12s | Interrupted Thread: %s (ID: %p)\n",
              static_cast<unsigned long> (g_trace_buffer[i].timestamp),
              g_trace_buffer[i].core,
              g_trace_buffer[i].event_name,
              g_trace_buffer[i].thread_name,
              g_trace_buffer[i].thread_id
            );
            uart::write (buf, static_cast<std::size_t> (len));
          }
        
        uart::write ("===========================================================\n\n");
        
        // Reset counter
        g_trace_count = 0;
        g_mutex.unlock ();
      }
    return nullptr;
  }

  // ---- Potentiometer -> clock selector (with y/Y confirmation) ------------
  // Reads GPIO40/ADC0, maps the 0..4095 range to one of the 12 SYS_TABLE rows.
  // When the pot settles on a DIFFERENT row than the one we booted at, it prints
  // the wanted clock and WAITS: the reset-into-new-clock only happens once the
  // user presses 'y'/'Y' on the console. 'n'/'N' (or turning the pot back to the
  // current clock) cancels; turning it to yet another band re-prompts. A short
  // debounce (stable for a few samples) gives band-edge hysteresis.
  constexpr unsigned POT_N = clocks::SYS_TABLE_LEN; // 12 bands
  constexpr int POT_DEBOUNCE = 4;                    // ~200 ms at 50 ms/sample

  // Drain all pending RX bytes and return the highest-priority action key seen:
  // ESC (27) beats 'y'/'Y' beats 'n'/'N'; -1 if none. Draining the whole FIFO
  // each poll means a key is never left stuck behind other bytes (e.g. a CR).
  int
  poll_key (void)
  {
    int best = -1;
    for (int i = 0; i < 64; ++i)
      {
        int c = uart::get_char ();
        if (c < 0)
          break;
        if (c == 27)
          best = 27;
        else if ((c == 'y' || c == 'Y') && best != 27)
          best = 'y';
        else if ((c == 'n' || c == 'N') && best < 0)
          best = 'n';
      }
    return best;
  }

  unsigned
  pot_band (void)
  {
    std::uint16_t raw = adc::read (rp2350::ADC_POT_CHANNEL);
    unsigned band = (static_cast<unsigned> (raw) * POT_N) / 4096u;
    return band >= POT_N ? POT_N - 1 : band;
  }

  void
  pot_prompt (unsigned band)
  {
    char b[128];
    int n = std::snprintf (
        b, sizeof (b),
        "\n[pot] wanted clock = %lu MHz (band %u).\n"
        "      press 'y' to apply (reset into it), 'n' to cancel.\n",
        static_cast<unsigned long> (clocks::SYS_TABLE[band].khz / 1000u), band);
    g_mutex.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_mutex.unlock ();
  }

  void*
  pot_thread (void*)
  {
    const unsigned booted = clocks::current_index ();
    int candidate = -1, stable = 0;
    int wanted = -1; // band awaiting y/Y confirmation, or -1 while monitoring

    for (;;)
      {
        unsigned band = pot_band ();
        int c = poll_key ();

        // ESC at any time: full reset back to the SAFE 48 MHz banner (index 0).
        // Resetting to a known-low clock (not the possibly-overclocked current
        // one) keeps the chip stable and easy to re-flash.
        if (c == 27)
          {
            g_mutex.lock ();
            uart::write ("\n[esc] full reset to 48 MHz banner...\n");
            g_mutex.unlock ();
            sysclock.sleep_for (20);
            rp2350::reg (0x400D8014u) = 0; // WATCHDOG SCRATCH2: no autostart -> gate
            clocks::reboot_to_index (0);
          }

        if (wanted < 0)
          {
            // Monitoring: wait for a stable band different from the running one.
            if (band == booted)
              {
                candidate = -1;
                stable = 0;
              }
            else if (static_cast<int> (band) == candidate)
              {
                if (++stable >= POT_DEBOUNCE)
                  {
                    wanted = static_cast<int> (band);
                    candidate = -1;
                    stable = 0;
                    pot_prompt (static_cast<unsigned> (wanted));
                  }
              }
            else
              {
                candidate = static_cast<int> (band);
                stable = 1;
              }
          }
        else
          {
            // Awaiting confirmation for 'wanted'.
            if (c == 'y' || c == 'Y')
              {
                g_mutex.lock ();
                uart::write ("[pot] confirmed: resetting...\n");
                g_mutex.unlock ();
                sysclock.sleep_for (20); // let the UART FIFO drain
                // Choosing the clock IS the confirmation to run: set the
                // autostart flag so the next boot skips the 'y' start gate.
                rp2350::reg (0x400D8014u) = 0xA5A57A57u; // WATCHDOG SCRATCH2
                clocks::reboot_to_index (static_cast<unsigned> (wanted));
              }
            else if (c == 'n' || c == 'N')
              {
                g_mutex.lock ();
                uart::write ("[pot] cancelled.\n");
                g_mutex.unlock ();
                wanted = -1;
                candidate = -1;
                stable = 0;
              }
            else if (band != static_cast<unsigned> (wanted))
              {
                // Pot moved while waiting: re-arm on the new stable band.
                if (static_cast<int> (band) == candidate)
                  {
                    if (++stable >= POT_DEBOUNCE)
                      {
                        candidate = -1;
                        stable = 0;
                        if (band == booted)
                          {
                            g_mutex.lock ();
                            uart::write ("[pot] back to current clock; cancelled.\n");
                            g_mutex.unlock ();
                            wanted = -1;
                          }
                        else
                          {
                            wanted = static_cast<int> (band);
                            pot_prompt (static_cast<unsigned> (wanted));
                          }
                      }
                  }
                else
                  {
                    candidate = static_cast<int> (band);
                    stable = 1;
                  }
              }
            else
              {
                candidate = -1;
                stable = 0;
              }
          }
        sysclock.sleep_for (50);
      }
    return nullptr;
  }

  // ---- Block linear solver (ported from smp-mat-test/rp2350B_psram) -------
  // Solves A x = b (N x N, diagonally dominant) by parallel BLOCK Gaussian
  // elimination: two solver threads pinned to cores 0/1, block size B, row
  // blocks distributed across the two cores, synchronized by generation-based
  // SIO-spinlock barriers; core 0 does the diagonal LU + back substitution.
  // Only this block-solving part is taken from smp-mat-test (no thread pool /
  // classical ground-truth demo). The matrices are static globals -> .bss ->
  // PSRAM window 1 (pico2-rp2350b-psram.ld), so the system is solved in PSRAM.
  // After the solution has been printed the threads return -> terminated ->
  // destroyed by the idle thread.
  // N=300 (B=50 -> 6 blocks). Two N×N float matrices (g_A + g_A_block) = 720 KB;
  // with the kernel .bss + 128 KB heap the total PSRAM footprint is ~1.4 MB, which
  // fits this board's 2 MB PSRAM. N=500 (2.3 MB) exceeded it: the high addresses
  // aliased onto low memory and corrupted the heap, hanging at the first malloc.
  constexpr int N = 800;
  constexpr int B = 100;

  class Barrier
  {
  public:
    void
    init (int count, std::uint32_t sio_slot)
    {
      target_count = count;
      current_count = 0;
      generation = 0;
      slot_ = sio_slot;
    }

    void
    wait (void)
    {
      __asm__ volatile ("dmb" ::: "memory");
      int gen = generation;

      // Acquire the SIO hardware spinlock (slot 2/3). On the dual-core RP2350
      // LDREX/STREX is per-core only, so a plain __sync spinlock would let both
      // cores hold it; the SIO spinlock is the RP2350 cross-core primitive.
      while (rp2350::reg (rp2350::SIO_SPINLOCK0 + 4 * slot_) == 0)
        {
          this_thread::yield ();
        }

      current_count = current_count + 1;
      if (current_count == target_count)
        {
          current_count = 0;
          generation = generation + 1;
          rp2350::reg (rp2350::SIO_SPINLOCK0 + 4 * slot_) = 1; // release
        }
      else
        {
          rp2350::reg (rp2350::SIO_SPINLOCK0 + 4 * slot_) = 1; // release
          while (generation == gen)
            {
              this_thread::yield ();
            }
        }

      __asm__ volatile ("dmb" ::: "memory");
    }

  private:
    int target_count;
    volatile int current_count;
    volatile int generation;
    std::uint32_t slot_;
  };

  static Barrier g_barrier_lu;
  static Barrier g_barrier_step;
  static volatile bool g_solver_done = false;
  static volatile std::uint32_t g_block_start_us = 0;
  static volatile std::uint32_t g_block_end_us = 0;

  // Solver matrices (static globals -> .bss -> PSRAM window 1).
  static float g_A[N][N];
  static float g_b[N];
  static float g_A_block[N][N];
  static float g_b_block[N];
  static float g_x_block[N];
  static float g_LU_k_T[B][B];
  static int g_pivot_k_T[B];

  static thread::stack::element_t solver_stacks[OS_NCPU][1024];
  static thread* solver_threads[OS_NCPU];
  static char solver_thread_names[OS_NCPU][16];

  // ---- Solver console helpers ----------------------------------------------
  void
  write_str (const char* s)
  {
    uart::write (s);
  }

  void
  write_fmt (const char* fmt, ...)
  {
    char b[96];
    std::va_list args;
    va_start (args, fmt);
    int n = std::vsnprintf (b, sizeof (b), fmt, args);
    va_end (args);
    if (n < 0)
      {
        n = 0;
      }
    if (n > static_cast<int> (sizeof (b)) - 1)
      {
        n = static_cast<int> (sizeof (b)) - 1;
      }
    uart::write (b, static_cast<std::size_t> (n));
  }

  void
  print_float (float val)
  {
    char b[32];
    if (val < 0.0f)
      {
        uart::write ("-");
        val = -val;
      }
    int int_part = static_cast<int> (val);
    int frac_part
        = static_cast<int> ((val - int_part) * 1000000.0f + 0.5f);
    if (frac_part >= 1000000)
      {
        int_part = int_part + 1;
        frac_part = frac_part - 1000000;
      }
    int n = std::snprintf (b, sizeof (b), "%d.%06d", int_part, frac_part);
    uart::write (b, static_cast<std::size_t> (n));
  }

  void
  print_solution (void);

  // ---- LU utilities for B x B blocks ---------------------------------------
  static bool
  lu_decompose (const float M[B][B], float LU[B][B], int pivot[B])
  {
    for (int i = 0; i < B; ++i)
      {
        for (int j = 0; j < B; ++j)
          {
            LU[i][j] = M[i][j];
          }
        pivot[i] = i;
      }
    for (int i = 0; i < B; ++i)
      {
        float max_val = 0.0f;
        int max_row = i;
        for (int r = i; r < B; ++r)
          {
            float val = std::abs (LU[r][i]);
            if (val > max_val)
              {
                max_val = val;
                max_row = r;
              }
          }
        if (max_val < 1e-9f)
          {
            return false; // Singular
          }
        if (max_row != i)
          {
            std::swap (pivot[i], pivot[max_row]);
            for (int c = 0; c < B; ++c)
              {
                std::swap (LU[i][c], LU[max_row][c]);
              }
          }
        for (int r = i + 1; r < B; ++r)
          {
            float factor = LU[r][i] / LU[i][i];
            LU[r][i] = factor;
            for (int c = i + 1; c < B; ++c)
              {
                LU[r][c] = LU[r][c] - factor * LU[i][c];
              }
          }
      }
    return true;
  }

  static void
  lu_solve (const float LU[B][B], const int pivot[B], const float Y[B],
            float X[B])
  {
    float temp[B];
    for (int i = 0; i < B; ++i)
      {
        temp[i] = Y[pivot[i]];
        for (int j = 0; j < i; ++j)
          {
            temp[i] = temp[i] - LU[i][j] * temp[j];
          }
      }
    for (int i = B - 1; i >= 0; --i)
      {
        X[i] = temp[i];
        for (int j = i + 1; j < B; ++j)
          {
            X[i] = X[i] - LU[i][j] * X[j];
          }
        X[i] = X[i] / LU[i][i];
      }
  }

  // Solve A_T * M_T = Src_T and transpose the result back to Dest.
  static void
  solve_block_transpose (const float LU_T[B][B], const int pivot_T[B],
                         const float Src[B][B], float Dest[B][B],
                         float Src_T[B][B], float M_T[B][B])
  {
    for (int i = 0; i < B; ++i)
      {
        for (int j = 0; j < B; ++j)
          {
            Src_T[i][j] = Src[j][i];
          }
      }
    for (int col = 0; col < B; ++col)
      {
        float y[B];
        float x[B];
        for (int r = 0; r < B; ++r)
          {
            y[r] = Src_T[r][col];
          }
        lu_solve (LU_T, pivot_T, y, x);
        for (int r = 0; r < B; ++r)
          {
            M_T[r][col] = x[r];
          }
      }
    for (int i = 0; i < B; ++i)
      {
        for (int j = 0; j < B; ++j)
          {
            Dest[i][j] = M_T[j][i];
          }
      }
  }

  static void
  mat_mul_sub (const float M[B][B], const float X[B][B], float Dest[B][B])
  {
    for (int i = 0; i < B; ++i)
      {
        for (int j = 0; j < B; ++j)
          {
            float sum = 0.0f;
            for (int k = 0; k < B; ++k)
              {
                sum = sum + M[i][k] * X[k][j];
              }
            Dest[i][j] = Dest[i][j] - sum;
          }
      }
  }

  static void
  vec_mul_sub (const float M[B][B], const float v[B], float Dest[B])
  {
    for (int i = 0; i < B; ++i)
      {
        float sum = 0.0f;
        for (int k = 0; k < B; ++k)
          {
            sum = sum + M[i][k] * v[k];
          }
        Dest[i] = Dest[i] - sum;
      }
  }

  static void
  get_matrix_block (const float Src[N][N], int br, int bc, float Dest[B][B])
  {
    for (int i = 0; i < B; ++i)
      {
        for (int j = 0; j < B; ++j)
          {
            Dest[i][j] = Src[br * B + i][bc * B + j];
          }
      }
  }

  static void
  set_matrix_block (float Dest[N][N], int br, int bc, const float Src[B][B])
  {
    for (int i = 0; i < B; ++i)
      {
        for (int j = 0; j < B; ++j)
          {
            Dest[br * B + i][bc * B + j] = Src[i][j];
          }
      }
  }

  // ---- Per-core solver worker (block parallel elimination) -----------------
  static void*
  solver_thread_func (void* arg)
  {
    int core_id = reinterpret_cast<int> (arg);
    int num_blocks = N / B;

    // Per-core workspace (static to keep thread stacks tiny). A_kj doubles as
    // the A_ik scratch: it is only needed again after solve_block_transpose.
    static float workspace_Src_T[OS_NCPU][B][B];
    static float workspace_M_T[OS_NCPU][B][B];
    static float M_local[OS_NCPU][B][B];
    static float A_kj[OS_NCPU][B][B];
    static float A_ij[OS_NCPU][B][B];

    if (core_id == 0)
      {
        g_block_start_us = timer_us32 ();
      }

    for (int k = 0; k < num_blocks - 1; ++k)
      {
        if (core_id == 0)
          {
            // Core 0: LU-decompose the diagonal block A_kk (transposed layout).
            static float A_kk_T[B][B];
            for (int i = 0; i < B; ++i)
              {
                for (int j = 0; j < B; ++j)
                  {
                    A_kk_T[i][j] = g_A_block[k * B + j][k * B + i];
                  }
              }
            lu_decompose (A_kk_T, g_LU_k_T, g_pivot_k_T);
          }

        g_barrier_lu.wait ();

        // All cores: compute multipliers + update their assigned row blocks.
        for (int i = k + 1; i < num_blocks; ++i)
          {
            if ((i - (k + 1)) % OS_NCPU == core_id)
              {
                get_matrix_block (g_A_block, i, k, A_kj[core_id]);
                solve_block_transpose (
                    g_LU_k_T, g_pivot_k_T, A_kj[core_id], M_local[core_id],
                    workspace_Src_T[core_id], workspace_M_T[core_id]);

                for (int j = k + 1; j < num_blocks; ++j)
                  {
                    get_matrix_block (g_A_block, k, j, A_kj[core_id]);
                    get_matrix_block (g_A_block, i, j, A_ij[core_id]);
                    mat_mul_sub (M_local[core_id], A_kj[core_id],
                                 A_ij[core_id]);
                    set_matrix_block (g_A_block, i, j, A_ij[core_id]);
                  }

                vec_mul_sub (M_local[core_id], &g_b_block[k * B],
                             &g_b_block[i * B]);
              }
          }

        g_barrier_step.wait ();
      }

    // Final block k = num_blocks - 1: sequential back substitution (core 0).
    if (core_id == 0)
      {
        static float A_ii[B][B];
        static float LU_ii[B][B];
        static float A_ij_temp[B][B];
        static int pivot_ii[B];
        static float temp_bi[B];

        for (int i = num_blocks - 1; i >= 0; --i)
          {
            for (int r = 0; r < B; ++r)
              {
                temp_bi[r] = g_b_block[i * B + r];
              }
            for (int j = i + 1; j < num_blocks; ++j)
              {
                get_matrix_block (g_A_block, i, j, A_ij_temp);
                for (int r = 0; r < B; ++r)
                  {
                    for (int c = 0; c < B; ++c)
                      {
                        temp_bi[r] = temp_bi[r]
                                     - A_ij_temp[r][c] * g_x_block[j * B + c];
                      }
                  }
              }
            get_matrix_block (g_A_block, i, i, A_ii);
            lu_decompose (A_ii, LU_ii, pivot_ii);
            lu_solve (LU_ii, pivot_ii, temp_bi, &g_x_block[i * B]);
          }

        g_block_end_us = timer_us32 ();
      }

    g_barrier_lu.wait (); // sync all cores at exit

    if (core_id == 0)
      {
        // Print the full solution, then set the done flag so os_main can wake
        // up and report. Both solver threads return right after -> the OS
        // terminates and destroys them.
        print_solution ();
        g_solver_done = true;
      }

    return nullptr; // thread exits -> terminated -> destroyed by the idle thread
  }

  void
  print_solution (void)
  {
    char b[96];
    g_mutex.lock ();
    uart::write ("\n==== BLOCK SOLVER (PSRAM) -> solution x = A^-1 b ====\n");
    for (int i = 0; i < N; ++i)
      {
        int n = std::snprintf (b, sizeof (b), "x[%3d] = ", i);
        uart::write (b, static_cast<std::size_t> (n));
        print_float (g_x_block[i]);
        uart::write ("\n");
      }
    uart::write ("==== block solve finished ====\n");
    g_mutex.unlock ();
  }

  void*
  secondary_idle_func (void*)
  {
    this_thread::thread ().priority (thread::priority::idle);

    // ---- NVIC Configuration for Nesting Demo (Core 1) -----------------------
    // 1. TIMER0 Alarm 2 (IRQ 2) - Priority 8 (high)
    NVIC_SetPriority (static_cast<IRQn_Type> (2), 8);
    NVIC_EnableIRQ (static_cast<IRQn_Type> (2));

    // 2. TIMER0 Alarm 3 (IRQ 3) - Priority 10 (medium-high)
    NVIC_SetPriority (static_cast<IRQn_Type> (3), 10);
    NVIC_EnableIRQ (static_cast<IRQn_Type> (3));

    // 3. UART0 (IRQ 33) - Priority 12 (lower than Alarm 2/3)
    NVIC_SetPriority (UART0_IRQn, 12);
    NVIC_EnableIRQ (UART0_IRQn);

    while (true)
      {
        __asm__ volatile ("dsb" ::: "memory");
        __asm__ volatile ("wfi");
        this_thread::yield ();
      }
    return nullptr;
  }
}

// ---- ISRs -----------------------------------------------------------------

extern "C"
{
  /*
   * UART0 Interrupt Handler (Priority 12 - medium)
   */
  void
  UART0_IRQ_Handler (void)
  {
    std::uint32_t entry_time = timer_us32 ();
    
    // Clear UART interrupt status
    rp2350::reg (rp2350::UART0_BASE + 0x44) = 0x7FF;
    
    log_trace_at ("UART_Start", entry_time);
    g_irq_uart = g_irq_uart + 1;

    // Busy-loop for 2.0 ms to ensure the 1.5 ms Timer2 interrupt preempts us.
    // At 150 MHz, this takes ~2 ms.
    volatile uint32_t count = 0;
    for (volatile int j = 0; j < 55000; ++j)
      {
        count++;
      }

    log_trace ("UART_End");
  }

  /*
   * TIMER0 Alarm 2 Handler (Priority 8 - highest)
   * Fires every 1.25 s (1250000 us).
   */
  void
  TIMER0_IRQ_2_Handler (void)
  {
    std::uint32_t entry_time = timer_us32 ();

    // Clear Timer0 Alarm 2 interrupt status bit (offset 0x3c on RP2350)
    rp2350::reg (TIMER0_BASE + 0x3C) = 1U << 2;

    // Rearm Alarm 2 for another 1.25 s (1250000 us)
    alarm2_arm (1250000);

    log_trace_at ("T2_Alarm", entry_time);
    g_irq_t2 = g_irq_t2 + 1;
  }

  /*
   * TIMER0 Alarm 3 Handler (Priority 10 - medium-high)
   * Fires every 1.2495 s (1249500 us).
   */
  void
  TIMER0_IRQ_3_Handler (void)
  {
    std::uint32_t entry_time = timer_us32 ();

    // Clear Timer0 Alarm 3 interrupt status bit (offset 0x3c on RP2350)
    rp2350::reg (TIMER0_BASE + 0x3C) = 1U << 3;

    // Rearm Alarm 3 for another 1.2495 s (1249500 us)
    alarm3_arm (1249500);

    log_trace_at ("T3_Alarm", entry_time);
    g_irq_t3 = g_irq_t3 + 1;
  }
}

// ---- Entry Point -----------------------------------------------------------

int
os_main (int /*argc*/, char* /*argv*/[])
{
  // Pin main thread to core 0 during startup
  this_thread::thread ().cpu_affinity (1u << 0);

  led::init ();
  timer_init ();

  // Potentiometer ADC (GPIO40/ADC0): clk_adc from PLL_USB, then enable ADC.
  clocks::adc_clk_init ();
  adc::init ();
  adc::gpio_init (rp2350::ADC_POT_PIN);

  {
    unsigned long mhz = static_cast<unsigned long> (clocks::current_khz () / 1000u);
    unsigned mv = static_cast<unsigned> (clocks::vsel_to_mv (clocks::current_vsel ()));
    char b[256];
    int n = std::snprintf (
        b, sizeof (b),
        "\n"
        "=============================================\n"
        "  Pico 2 / RP2350B - micro-os-plus-iii_____________\n"
        "  smp-test-nested-clock : pot -> clock -> SMP nested test\n"
        "  solve linear system by blocks (PSRAM)\n"
        "  clock = %lu MHz  (VREG %u.%02u V, table idx %u)\n"
        "=============================================\n",
        mhz, mv / 1000u, (mv % 1000u) / 10u, clocks::current_index ());
    if (n > static_cast<int> (sizeof (b) - 1))
      n = sizeof (b) - 1;
    uart::write (b, static_cast<std::size_t> (n));
  }

  // ---- Gate: show clock + live GPIO40 tension until the user presses y -------
  // ...UNLESS we just rebooted from a pot clock-selection 'y' (WATCHDOG SCRATCH2
  // = autostart magic): choosing the clock already confirmed the run, so start
  // directly with no second 'y'. ESC and cold boot leave the flag clear -> gate.
  bool autostart = (rp2350::reg (0x400D8014u) == 0xA5A57A57u);
  rp2350::reg (0x400D8014u) = 0; // consume the flag either way
  if (!autostart)
    {
      uart::write ("\npress 'y' to start the RTOS / SMP nested test at this clock.\n");
      (void) poll_key (); // drain stale input
      const unsigned long now_mhz
          = static_cast<unsigned long> (clocks::current_khz () / 1000u);
      for (;;)
        {
          std::uint16_t raw = adc::read (rp2350::ADC_POT_CHANNEL);
          unsigned mv = (static_cast<unsigned> (raw) * 3300u) / 4096u;
          unsigned band = (static_cast<unsigned> (raw) * clocks::SYS_TABLE_LEN) / 4096u;
          if (band >= clocks::SYS_TABLE_LEN)
            band = clocks::SYS_TABLE_LEN - 1;
          char b[96];
          int n = std::snprintf (
              b, sizeof (b),
              "\rGPIO40 = %u.%03u V (adc %4u) | now %lu MHz | pot->%lu MHz  press 'y'... ",
              mv / 1000u, mv % 1000u, raw, now_mhz,
              static_cast<unsigned long> (clocks::SYS_TABLE[band].khz / 1000u));
          uart::write (b, static_cast<std::size_t> (n));

          int c = poll_key ();
          if (c == 'y' || c == 'Y')
            {
              // If the pot is already on a DIFFERENT band than the clock we
              // booted at, a single 'y' should select AND run it: reboot into
              // that clock with the autostart flag so the next boot skips this
              // gate. If the pot matches the current clock, just start here.
              if (band != clocks::current_index ())
                {
                  uart::write ("\nstarting at selected clock...\n");
                  rp2350::reg (0x400D8014u) = 0xA5A57A57u; // autostart after reboot
                  sysclock.sleep_for (20);
                  clocks::reboot_to_index (band); // never returns
                }
              break; // pot matches the current clock -> start right here
            }
          sysclock.sleep_for (300);
        }
    }
  uart::write ("\nstarting...\n");

  // Register Core 1 idle thread
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

  // Create threads
  thread::attributes attr = thread::initializer;

  // Worker threads with default (any core) affinity -> no pinning
  // Names must be STATIC: thread stores the name pointer (not a copy), so a
  // stack-local buffer would dangle once os_main's stack is reused (the trace
  // dump then reads garbage as the thread name).
  static char worker_names[2][8];
  static thread* workers[2];
  for (int i = 0; i < 2; ++i)
    {
      worker_ctx[i] = ctx{ i, 0 };
      attr.th_stack_address = worker_stacks[i];
      attr.th_stack_size_bytes = sizeof (worker_stacks[i]);
      std::snprintf (worker_names[i], sizeof (worker_names[i]), "wrk%d", i);
      workers[i] = new thread (worker_names[i], worker, &worker_ctx[i], attr);
    }

  // LED thread with default (any core) affinity -> no pinning
  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_th{ "led", led_thread, nullptr, attr };

  // Trigger thread with default (any core) affinity -> no pinning
  attr.th_stack_address = trigger_stack;
  attr.th_stack_size_bytes = sizeof (trigger_stack);
  static thread trigger_th{ "trigger", trigger_thread, nullptr, attr };

  // Trace printer thread
  attr.th_stack_address = trace_stack;
  attr.th_stack_size_bytes = sizeof (trace_stack);
  static thread trace_th{ "trace", trace_printer_thread, nullptr, attr };

  // Potentiometer poller (any core) -> reboots into the selected clock.
  attr.th_stack_address = pot_stack;
  attr.th_stack_size_bytes = sizeof (pot_stack);
  static thread pot_th{ "pot", pot_thread, nullptr, attr };

  // ---- NVIC Configuration for Nesting Demo (Core 0) -----------------------
  
  // 1. TIMER0 Alarm 2 (IRQ 2) - Priority 8 (high)
  NVIC_SetPriority (static_cast<IRQn_Type> (2), 8);
  alarm2_arm (1250000); // arm in 1.25s
  rp2350::reg (TIMER0_BASE + 0x40) |= (1U << 2); // Enable Alarm 2 in Timer INTE
  NVIC_EnableIRQ (static_cast<IRQn_Type> (2));

  // 2. TIMER0 Alarm 3 (IRQ 3) - Priority 10 (medium-high)
  NVIC_SetPriority (static_cast<IRQn_Type> (3), 10);
  alarm3_arm (1249500); // arm in 1.2495s
  rp2350::reg (TIMER0_BASE + 0x40) |= (1U << 3); // Enable Alarm 3 in Timer INTE
  NVIC_EnableIRQ (static_cast<IRQn_Type> (3));

  // 3. UART0 (IRQ 33) - Priority 12 (lower than Alarm 2/3)
  NVIC_SetPriority (UART0_IRQn, 12);
  NVIC_EnableIRQ (UART0_IRQn);

  uart::write ("core 0: launching core 1 (bootrom FIFO handshake)...\n");
  multicore::launch_core1 (port_smp_secondary_start,
                           port_core1_stack_top ());
  port_smp_core0_ipi_enable ();
  uart::write ("core 0: core 1 launched; SMP running without worker affinity.\n");

  // ---- Block linear solver (PSRAM) ------------------------------------------
  // The system A x = b is generated and solved entirely in PSRAM (.bss window
  // 1). Two solver threads, pinned to cores 0/1, run the parallel block
  // elimination; once the solution has been printed they exit and are destroyed.
  if (N % B != 0)
    {
      uart::write ("ERROR: N must be divisible by B!\n");
      for (;;)
        {
          __asm__ volatile ("wfi");
        }
    }
  if ((N / B) % OS_NCPU != 0)
    {
      uart::write ("ERROR: number of blocks N/B must be a multiple of cores!\n");
      for (;;)
        {
          __asm__ volatile ("wfi");
        }
    }

  g_barrier_lu.init (OS_NCPU, 2);   // SIO spinlock slot 2
  g_barrier_step.init (OS_NCPU, 3); // SIO spinlock slot 3
  g_solver_done = false;

  {
    char m[160];
    int mn = std::snprintf (
        m, sizeof (m),
        "matrix A @ 0x%08x [%s] (N = %d, B = %d, cores = %d)\n",
        reinterpret_cast<unsigned int> (&g_A[0][0]),
        (reinterpret_cast<unsigned int> (&g_A[0][0]) >= 0x11000000U
         && reinterpret_cast<unsigned int> (&g_A[0][0]) < 0x11800000U)
            ? "PSRAM"
            : "SRAM",
        N, B, OS_NCPU);
    uart::write (m, static_cast<std::size_t> (mn));
  }

  uart::write ("Generating diagonally dominant matrix in PSRAM...\n");
  std::srand (42);
  for (int i = 0; i < N; ++i)
    {
      for (int j = 0; j < N; ++j)
        {
          g_A[i][j]
              = -5.0f + static_cast<float> (std::rand ())
                            / (static_cast<float> (RAND_MAX) / 10.0f);
        }
    }
  for (int i = 0; i < N; ++i)
    {
      float sum = 0.0f;
      for (int j = 0; j < N; ++j)
        {
          if (i != j)
            {
              sum = sum + std::abs (g_A[i][j]);
            }
        }
      g_A[i][i] = sum + 5.0f; // strictly diagonally dominant
      g_b[i] = 1.0f;
    }
  for (int i = 0; i < N; ++i)
    {
      for (int j = 0; j < N; ++j)
        {
          g_A_block[i][j] = g_A[i][j];
        }
      g_b_block[i] = g_b[i];
    }

  // ---- Launch the parallel solver threads (pinned to cores 0 and 1) --------
  uart::write ("Starting parallel 2-core block elimination...\n");
  for (int c = 0; c < OS_NCPU; ++c)
    {
      thread::attributes attr = thread::initializer;
      attr.th_stack_address = solver_stacks[c];
      attr.th_stack_size_bytes = sizeof (solver_stacks[c]);
      std::snprintf (solver_thread_names[c], 16, "solver_c%d", c);
      solver_threads[c]
          = new thread (solver_thread_names[c], solver_thread_func,
                        reinterpret_cast<void*> (c), attr);
      solver_threads[c]->cpu_affinity (1u << c);
    }

  while (!g_solver_done)
    {
      sysclock.sleep_for (100);
    }
  std::uint32_t block_us = g_block_end_us - g_block_start_us;
  {
    char m[96];
    int n = std::snprintf (
        m, sizeof (m),
        "Block elimination finished in %lu us; solver threads destroyed.\n",
        static_cast<unsigned long> (block_us));
    uart::write (m, static_cast<std::size_t> (n));
  }

  // Bounded run: each worker does kIters beats (~kIters * 1 s), then the
  // verdict -- both workers ran them all, and all three nesting interrupts
  // (UART0, TIMER0 alarms 2 and 3) fired.
  constexpr unsigned kIters = 20;
  for (unsigned i = 0; i < 600u
                       && (worker_ctx[0].local < kIters
                           || worker_ctx[1].local < kIters);
       ++i)
    {
      sysclock.sleep_for (100);
    }

  const bool ok = (worker_ctx[0].local >= kIters)
                  && (worker_ctx[1].local >= kIters) && (g_irq_uart > 0)
                  && (g_irq_t2 > 0) && (g_irq_t3 > 0);
  char b[128];
  int n = std::snprintf (
      b, sizeof (b),
      "\nRESULT: %s  (wrk0=%u wrk1=%u uart=%u t2=%u t3=%u)\n",
      ok ? "PASS" : "FAIL", worker_ctx[0].local, worker_ctx[1].local,
      g_irq_uart, g_irq_t2, g_irq_t3);
  g_mutex.lock ();
  uart::write (b, static_cast<std::size_t> (n));
  g_mutex.unlock ();

  // Ends through the strong _Exit() -> semihosting SYS_EXIT; hw_result also
  // reports RESULT on the semihosting console the runner reads.
  if (ok)
    {
      hw_result::ok ();
    }
  else
    {
      hw_result::fail ();
    }
  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
