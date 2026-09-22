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

#include <bsp/led.hpp>
#include <bsp/multicore.hpp>
#include <bsp/rp2350.hpp>
#include <bsp/uart.hpp>
#include <cmsis_device.h>

#include <cstdio>
#include <cstdint>

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

  struct ctx
  {
    int id;
    unsigned local;
  };
  ctx worker_ctx[2];

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

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico 2 / RP2350B - micro-os-plus-iii\n");
  uart::write ("  smp-test-nested : Nested Interrupts & SMP\n");
  uart::write ("=============================================\n");

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
  static thread* workers[2];
  for (int i = 0; i < 2; ++i)
    {
      worker_ctx[i] = ctx{ i, 0 };
      attr.th_stack_address = worker_stacks[i];
      attr.th_stack_size_bytes = sizeof (worker_stacks[i]);
      char name[8];
      std::snprintf (name, sizeof (name), "wrk%d", i);
      workers[i] = new thread (name, worker, &worker_ctx[i], attr);
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

  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
