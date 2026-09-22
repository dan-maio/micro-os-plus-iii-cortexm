/*
 * smp-test2 (Pico 2 / RP2350B) — Phase 3: dual-core SMP RTOS.
 *
 * The shared micro-os-plus-iii kernel runs in its SMP variant on BOTH
 * Cortex-M33 cores (OS_USE_SMP_SCHEDULER + OS_NCPU 2), on top of the pico2
 * hand-written M33 SMP port:
 *   - per-core PendSV context switch + per-core SysTick (core 0 ticks the
 *     RTOS clock, core 1 only reschedules);
 *   - affinity scheduler (worker0 pinned to core 0, worker1 pinned to core 1);
 *   - kernel spinlock (SIO hardware spinlock 0) protecting the shared data;
 *   - core-1 launched via the bootrom FIFO into port_smp_secondary_start.
 *
 * Proves:
 *   - both cores running the scheduler (each worker prints its SIO_CPUID);
 *   - cross-core shared state (global counter under a mutex);
 *   - system clock (1 ms tick, advanced by core 0);
 *   - LED heartbeat on core 0.
 *
 * Output on UART0 @115200. The single-core Phase-2 test is smp-test1
 * (unchanged, no OS_USE_SMP_SCHEDULER).
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/led.hpp>
#include <bsp/multicore.hpp>
#include <bsp/uart.hpp>

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
    char b[96];
    int n = std::snprintf (
        b, sizeof (b), "[%8lu ms] %-6s #%d core=%u local=%-4u global=%u\n",
        static_cast<unsigned long> (sysclock.now ()), role, id,
        port_cpu_id (), local, global);
    g_mutex.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_mutex.unlock ();
  }

  void*
  worker (void* arg)
  {
    auto* c = static_cast<ctx*> (arg);
    // Pin this thread to its core (also set in os_main). A freshly created
    // thread has default all-core affinity and could briefly be picked by
    // the wrong core before os_main calls cpu_affinity(); pinning here and
    // yielding hands it to the right core before any work is done.
    this_thread::thread ().cpu_affinity (1u << c->id);
    if (port_cpu_id () != static_cast<unsigned> (c->id))
      {
        this_thread::yield ();
      }
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
    bool on = false;
    for (;;)
      {
        g_beats.wait (); // one blink per worker beat
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
    return nullptr;
  }

  // Core-1 idle thread (registered in os_idle_thread_core[1] before the
  // launch; the kernel's is_thread_allowed_on_cpu pins it to core 1 by the
  // reserved "idle1" name).
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
  // Pin the main thread (which launches core 1 and owns os_main) to core 0:
  // its default affinity is all cores, so core 1's affinity picker could
  // otherwise grab it mid-launch.
  this_thread::thread ().cpu_affinity (1u << 0);

  led::init ();

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico 2 / RP2350B - micro-os-plus-iii\n");
  uart::write ("  smp-test2 : dual-core SMP RTOS\n");
  uart::write ("=============================================\n");

  // Core-1 idle thread + registration.
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

  uart::write ("core 0: idle1 installed; creating worker/LED threads...\n");

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
      // Pin worker 0 to core 0 and worker 1 to core 1.
      workers[i]->cpu_affinity (1u << i);
    }

  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_th{ "led", led_thread, nullptr, attr };
  led_th.cpu_affinity (1u << 0);

  uart::write ("core 0: launching core 1 (bootrom FIFO handshake)...\n");
  // The port does NOT enable the SIO FIFO IRQ (25) until after the launch;
  // the FIFO is busy carrying the bootrom handshake words here, so the IPI
  // handler must stay off. Re-enable (core 0 side) only after the launch.
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
