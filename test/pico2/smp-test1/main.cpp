/*
 * smp-test1 (Pico 2 / RP2350) — Phase 2: single-core RTOS bring-up.
 *
 * The shared micro-os-plus-iii kernel runs on ONE Cortex-M33 core (core 0),
 * using its reference scheduler on top of the upstream Cortex-M PendSV context
 * switch + the core SysTick tick (OS_USE_SMP_SCHEDULER undefined). Proves:
 *   - scheduler + context switch (3 threads)
 *   - system clock (sysclock.sleep_for / now, 1 tick == 1 ms)
 *   - plain mutex (shared, protected global counter)
 *   - counting semaphore (worker -> led handoff)
 *   - LED heartbeat (GPIO25) as a visual "RTOS is alive"
 *
 * Output on UART0 @115200. The 2-core SMP variant comes in Phase 3.
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/uart.hpp>
#include <bsp/led.hpp>

#include <cstdio>
#include <cstdint>

using namespace os::rtos;

namespace
{
  unsigned g_global = 0;
  mutex g_mutex{ "g" };
  semaphore_counting g_beats{ "beats", 8, 0 };

  thread::stack::element_t worker_stacks[2][1024];
  thread::stack::element_t led_stack[1024];

  struct ctx
  {
    int id;
    unsigned local;
  };
  ctx worker_ctx[2];

  void
  line (const char* role, int id, unsigned local, unsigned global)
  {
    char b[80];
    int n = std::snprintf (
        b, sizeof (b), "[%8lu ms] %-6s #%d  local=%-4u global=%u\n",
        static_cast<unsigned long> (sysclock.now ()), role, id, local, global);
    g_mutex.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_mutex.unlock ();
  }

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
        g_beats.post (); // signal the LED thread
        sysclock.sleep_for (500);
      }
    return nullptr;
  }

  void*
  led_thread (void*)
  {
    // One blink per worker beat. The two workers run in lockstep, so their
    // beats arrive ~2 ms apart; without a minimum ON time the LED would
    // toggle ON/OFF almost instantly and be barely visible. Each beat now
    // produces a 100 ms visible pulse.
    for (;;)
      {
        g_beats.wait (); // one blink per worker beat
        led::on ();
        sysclock.sleep_for (100);
        led::off ();
      }
    return nullptr;
  }
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico 2 / RP2350 - micro-os-plus-iii\n");
  uart::write ("  smp-test1 : single-core RTOS (core 0)\n");
  uart::write ("=============================================\n");

  thread::attributes attr = thread::initializer;

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

  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_th{ "led", led_thread, nullptr, attr };

  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
