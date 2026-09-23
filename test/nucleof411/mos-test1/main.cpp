/*
 * mos-test1 — Nucleo-F411RE single-CPU acceptance test for micro-os-plus-iii.
 *
 * One producer/consumer application that exercises, on a bare Cortex-M4 with
 * SysTick + NVIC + PendSV, every core RTOS mechanism at once:
 *
 *   - plain mutex          : protects the shared global counter
 *   - recursive mutex      : serialises the console; taken nested (banner ->
 *                            console_line) to prove recursive ownership
 *   - counting semaphores  : the classic empty/full bounded-buffer pair
 *   - system clock         : sysclock.sleep_for() pacing + sysclock.now()
 *                            timestamps (1 tick == 1 ms at 1000 Hz SysTick)
 *   - scheduler / switch   : 3 producers + 3 consumers + 2 down-counting
 *                            consumers (local starts at 10000) + 1 LED thread
 *
 * Output channels (as specified):
 *   - USART2 @ 115200 8N1 (ST-Link VCP): every worker prints, twice a second,
 *     its role, thread number, its own local counter, and the shared global
 *     counter (incremented by all threads under the plain mutex).
 *   - ARM semihosting (os::trace::printf): the LED thread prints "led on"/"led off".
 *   - LED LD2 (PA5): blinks at a 1 s interval.
 *
 * The very same micro-os-plus-iii kernel used by the Cortex-A7 SMP port runs
 * here. The SMP additions are gated by OS_USE_SMP_SCHEDULER (defined by the A7
 * port, undefined here), so this build gets the stock scalar reference
 * scheduler on top of the Cortex-M PendSV context switch.
 */

#include <cmsis-plus/rtos/os.h>
#include <cmsis-plus/diag/trace.h>

#include <board.hpp>
#include <led.hpp>
#include <uart.hpp>
#include <hw_result.hpp>

#include <cstdio>
#include <cstdint>

using namespace os::rtos;

// ----------------------------------------------------------------------------
// Shared resources.

namespace
{
  constexpr int BUFFER_SIZE = 8;
  constexpr int PRODUCERS = 3;
  constexpr int CONSUMERS = 3;
  constexpr int CONSUMERS_DEC = 2;               // down-counting consumers
  constexpr unsigned DEC_START = 10000;          // their local counter starts here

  constexpr unsigned WORKER_PERIOD_MS = 500;  // "twice a second"
  constexpr unsigned LED_PERIOD_MS = 1000;    // 1 s blink interval

  // Bounded buffer, guarded by g_buffer_mutex + the two counting semaphores.
  struct ring_buffer
  {
    int data[BUFFER_SIZE];
    int head;
    int tail;
  };
  ring_buffer g_buffer{};

  mutex g_buffer_mutex{ "buffer" };                     // plain
  semaphore_counting g_sem_free{ "free", BUFFER_SIZE, BUFFER_SIZE };
  semaphore_counting g_sem_used{ "used", BUFFER_SIZE, 0 };

  // Shared counter incremented by every thread, under a plain mutex.
  unsigned g_global_counter = 0;
  mutex g_count_mutex{ "count" };

  // Console serialisation, using a RECURSIVE mutex so a holder may re-enter
  // (banner() locks it and calls console_line(), which locks it again).
  mutex_recursive g_console_mutex{ "console" };

  // Per-thread context.
  struct worker_ctx
  {
    const char* role;
    int id;
    unsigned local_counter;
  };

  // ----- helpers -----------------------------------------------------------

  unsigned
  bump_global (void)
  {
    g_count_mutex.lock ();
    unsigned v = ++g_global_counter;
    g_count_mutex.unlock ();
    return v;
  }

  // Emit one console line atomically. Locks the recursive console mutex.
  void
  console_line (const char* text)
  {
    g_console_mutex.lock ();
    uart::write (text);
    g_console_mutex.unlock ();
  }

  void
  console_worker (const worker_ctx& ctx, unsigned global_snapshot)
  {
    char line[96];
    int n = std::snprintf (
        line, sizeof (line), "[%8lu ms] %-8s #%d  local=%-5u  global=%u\n",
        static_cast<unsigned long> (sysclock.now ()), ctx.role, ctx.id,
        ctx.local_counter, global_snapshot);
    if (n < 0)
      {
        return;
      }
    g_console_mutex.lock ();
    uart::write (line, static_cast<std::size_t> (n));
    g_console_mutex.unlock ();
  }

  // Multi-line banner printed while HOLDING the recursive mutex, then calling
  // console_line() (which locks it again) -> proves recursive re-entry works.
  void
  banner (void)
  {
    g_console_mutex.lock ();          // depth 1
    console_line ("=============================================\n");   // depth 2
    console_line ("  Nucleo-F411RE  micro-os-plus-iii  mos-test1\n");
    console_line ("  producers/consumers + mutex/recursive/sem\n");
    console_line ("=============================================\n");
    g_console_mutex.unlock ();        // back to depth 0
  }

  // ----- threads -----------------------------------------------------------

  void*
  producer_func (void* arg)
  {
    auto* ctx = static_cast<worker_ctx*> (arg);
    int value = ctx->id * 1000;
    for (;;)
      {
        g_sem_free.wait ();                 // wait for a free slot

        g_buffer_mutex.lock ();
        g_buffer.data[g_buffer.head] = value;
        g_buffer.head = (g_buffer.head + 1) % BUFFER_SIZE;
        g_buffer_mutex.unlock ();

        g_sem_used.post ();                 // signal an item is available

        ctx->local_counter++;
        unsigned g = bump_global ();
        console_worker (*ctx, g);

        ++value;
        sysclock.sleep_for (WORKER_PERIOD_MS);
      }
    return nullptr;
  }

  void*
  consumer_func (void* arg)
  {
    auto* ctx = static_cast<worker_ctx*> (arg);
    for (;;)
      {
        g_sem_used.wait ();                 // wait for an item

        g_buffer_mutex.lock ();
        (void) g_buffer.data[g_buffer.tail];
        g_buffer.tail = (g_buffer.tail + 1) % BUFFER_SIZE;
        g_buffer_mutex.unlock ();

        g_sem_free.post ();                 // release the slot

        ctx->local_counter++;
        unsigned g = bump_global ();
        console_worker (*ctx, g);

        sysclock.sleep_for (WORKER_PERIOD_MS);
      }
    return nullptr;
  }

  // Same bounded-buffer consumer, but its per-thread local counter counts DOWN
  // from DEC_START (10000) instead of up. Proves per-thread state is independent
  // and that extra consumers share the semaphore/buffer safely.
  void*
  consumer_dec_func (void* arg)
  {
    auto* ctx = static_cast<worker_ctx*> (arg);
    for (;;)
      {
        g_sem_used.wait ();                 // wait for an item

        g_buffer_mutex.lock ();
        (void) g_buffer.data[g_buffer.tail];
        g_buffer.tail = (g_buffer.tail + 1) % BUFFER_SIZE;
        g_buffer_mutex.unlock ();

        g_sem_free.post ();                 // release the slot

        ctx->local_counter--;               // count DOWN from DEC_START
        unsigned g = bump_global ();
        console_worker (*ctx, g);

        sysclock.sleep_for (WORKER_PERIOD_MS);
      }
    return nullptr;
  }

  void*
  led_func (void* arg)
  {
    auto* ctx = static_cast<worker_ctx*> (arg);
    bool state = false;
    for (;;)
      {
        state = !state;
        if (state)
          {
            led::on ();
            os::trace::printf ("led on\n");      // semihosting console
          }
        else
          {
            led::off ();
            os::trace::printf ("led off\n");
          }
        ctx->local_counter++;
        bump_global ();
        sysclock.sleep_for (LED_PERIOD_MS);
      }
    return nullptr;
  }

  // ----- storage -----------------------------------------------------------

  thread::stack::element_t producer_stacks[PRODUCERS][1024];
  thread::stack::element_t consumer_stacks[CONSUMERS][1024];
  thread::stack::element_t consumer_dec_stacks[CONSUMERS_DEC][1024];
  thread::stack::element_t led_stack[1024];

  worker_ctx producer_ctx[PRODUCERS];
  worker_ctx consumer_ctx[CONSUMERS];
  worker_ctx consumer_dec_ctx[CONSUMERS_DEC];
  worker_ctx led_ctx{ "led", 0, 0 };
}

// ----------------------------------------------------------------------------

int
os_main (int /*argc*/, char* /*argv*/[])
{
  // Bring up the board peripherals (the RTOS tick/scheduler are already
  // running; this thread is os_main).
  led::init ();
  uart::init (board::UART_BAUD);

  banner ();

  thread::attributes attr = thread::initializer;

  static thread* producers[PRODUCERS];
  for (int i = 0; i < PRODUCERS; ++i)
    {
      producer_ctx[i] = worker_ctx{ "producer", i, 0 };
      attr.th_stack_address = producer_stacks[i];
      attr.th_stack_size_bytes = sizeof (producer_stacks[i]);
      char name[16];
      std::snprintf (name, sizeof (name), "prod%d", i);
      producers[i]
          = new thread (name, producer_func, &producer_ctx[i], attr);
    }

  static thread* consumers[CONSUMERS];
  for (int i = 0; i < CONSUMERS; ++i)
    {
      consumer_ctx[i] = worker_ctx{ "consumer", i, 0 };
      attr.th_stack_address = consumer_stacks[i];
      attr.th_stack_size_bytes = sizeof (consumer_stacks[i]);
      char name[16];
      std::snprintf (name, sizeof (name), "cons%d", i);
      consumers[i]
          = new thread (name, consumer_func, &consumer_ctx[i], attr);
    }

  static thread* consumers_dec[CONSUMERS_DEC];
  for (int i = 0; i < CONSUMERS_DEC; ++i)
    {
      consumer_dec_ctx[i] = worker_ctx{ "cons-dec", i, DEC_START };
      attr.th_stack_address = consumer_dec_stacks[i];
      attr.th_stack_size_bytes = sizeof (consumer_dec_stacks[i]);
      char name[16];
      std::snprintf (name, sizeof (name), "consd%d", i);
      consumers_dec[i]
          = new thread (name, consumer_dec_func, &consumer_dec_ctx[i], attr);
    }

  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_thread{ "led", led_func, &led_ctx, attr };

  // Let the workers run for a bounded time, then check the shared counter
  // moved and end the run with the verdict (semihosting exit).
  constexpr unsigned kCheckMs = 5000;
  sysclock.sleep_for (kCheckMs);
  {
    const bool ok = (g_global_counter > 0);
    char rb[80];
    std::snprintf (rb, sizeof (rb), "\nRESULT: %s  (global=%u)\n",
                   ok ? "PASS" : "FAIL", g_global_counter);
    console_line (rb);
    if (ok)
      {
        hw_result::ok ();
      }
    else
      {
        hw_result::fail ();
      }
  }

  // os_main has nothing more to do; keep it alive so the workers run.
  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
