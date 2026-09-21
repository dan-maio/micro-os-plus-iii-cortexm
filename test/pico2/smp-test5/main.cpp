/*
 * smp-test5 (Pico 2 / RP2350) — Phase 6: interrupt-driven UART TX,
 * cross-core ISR.
 *
 * The FIRST real peripheral interrupt on the fixed vector table: UART0
 * (PL011) external IRQ 33, serviced on CORE 1 (cross-core routing), draining
 * a producer/consumer TX ring that a thread on core 0 fills:
 *
 *   producer (core 0)                    irq_host (core 1)
 *     sem_uart_ready.wait ()               NVIC_EnableIRQ(UART0_IRQn)
 *     txq_write (line)                     g_tx_active = true
 *       lock, enqueue ring                 sem_uart_ready.post ()
 *       set IMSC.TXIM (kick)            UART0_IRQ_Handler (IRQ 33, core 1)
 *       sleep (throttle)                    drain ring -> UART FIFO
 *                                           clear TXIM when ring empty
 *
 * Proves:
 *   - the external-IRQ vector table routes IRQ 33 to UART0_IRQ_Handler;
 *   - the ISR runs on the SECONDARY core (g_isr_core == 1);
 *   - interrupt-driven TX: no thread busy-polls the UART after boot;
 *   - byte integrity: enqueued == transmitted, 0 dropped.
 *
 * Per project policy the shared uart.cpp is NOT linked here: smp-test5 ships
 * its own PL011 driver (polled for the boot banner, interrupt-driven after),
 * so the shared polled driver is untouched.
 *
 * Output on UART0 @115200, LED blinks per 100 lines (core 0).
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/led.hpp>
#include <bsp/multicore.hpp>
#include <bsp/rp2350.hpp>
#include <cmsis_device.h>

#include <cstddef>
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

// ----------------------------------------------------------------------------
// Per-test PL011 UART driver: polled while g_tx_active == false (boot banner),
// then interrupt-driven through a TX ring drained by UART0_IRQ_Handler.
// ----------------------------------------------------------------------------

namespace
{
  // PL011 register offsets.
  constexpr std::uint32_t DR = 0x00, FR = 0x18, IBRD = 0x24, FBRD = 0x28,
                          LCRH = 0x2C, CR = 0x30, IFLS = 0x34, IMSC = 0x38,
                          ICR = 0x44;
  constexpr std::uint32_t FR_TXFF = 1U << 5;
  constexpr std::uint32_t IMSC_TXIM = 1U << 5;   // TX interrupt mask
  // TX trigger 4/8 (IRQ when the FIFO has <=8 bytes) — bits [3:2].
  constexpr std::uint32_t IFLS_TXIFLSEL_4_8 = 2U << 2;

  constexpr std::uint32_t GPIO_FUNC_UART0 = 2;
  constexpr std::uint32_t PADS_ISO = 1U << 8, PADS_IE = 1U << 6;
  constexpr std::uint32_t RESET_UART0 = 1U << 26, RESET_IO_BANK0 = 1U << 6,
                          RESET_PADS_BANK0 = 1U << 9;

  void
  unreset_block (std::uint32_t mask)
  {
    rp2350::reg (rp2350::RESETS_BASE + 0x00) &= ~mask;
    while ((rp2350::reg (rp2350::RESETS_BASE + 0x08) & mask) != mask)
      {
      }
  }

  void
  polled_put_char (char c)
  {
    while (rp2350::reg (rp2350::UART0_BASE + FR) & FR_TXFF)
      {
      }
    rp2350::reg (rp2350::UART0_BASE + DR)
        = static_cast<std::uint32_t> (static_cast<unsigned char> (c));
  }

  void
  polled_put_str (const char* s, std::size_t n)
  {
    for (std::size_t i = 0; i < n; ++i)
      {
        if (s[i] == '\n')
          {
            polled_put_char ('\r');
          }
        polled_put_char (s[i]);
      }
  }

  // ---- TX ring (single producer + ISR consumer, power of two) --------------
  constexpr std::uint32_t TXQ_SIZE = 2048;
  char g_txq[TXQ_SIZE];
  volatile std::uint32_t g_txq_head = 0;   // producers write here
  volatile std::uint32_t g_txq_tail = 0;   // ISR consumes here
  volatile bool g_tx_active = false;       // false = polled (boot)
  volatile std::uint32_t g_tx_enq = 0;     // bytes enqueued (producers)
  volatile std::uint32_t g_tx_bytes = 0;   // bytes transmitted (ISR)
  volatile std::uint32_t g_tx_dropped = 0;
  volatile std::uint32_t g_tx_irq_count = 0;
  volatile std::uint32_t g_isr_core = 0xFF;

  // Cross-core ring lock: SIO HARDWARE spinlock 2 (slots 0/1 are the kernel
  // and console locks). NOT a plain LDREX/STREX spinlock — on the dual-core
  // RP2350 the exclusive monitor is per-core, so two cores can both "hold" a
  // __atomic_exchange lock and clobber the ring.
  constexpr std::uint32_t TXQ_LOCK = rp2350::SIO_SPINLOCK0 + 0x08; // slot 2

  // Lock: mask local IRQs BEFORE spinning, restore the ORIGINAL PRIMASK.
  std::uint32_t
  txq_lock_acquire (void)
  {
    std::uint32_t saved = __get_PRIMASK ();
    __asm__ volatile ("cpsid i" ::: "memory");
    while (rp2350::reg (TXQ_LOCK) == 0)
      {
      }
    return saved;
  }

  void
  txq_lock_release (std::uint32_t saved)
  {
    rp2350::reg (TXQ_LOCK) = 1;
    __set_PRIMASK (saved);
  }

  // Caller holds the lock. '\n' expands to CR+LF. Returns the number of
  // WIRE bytes actually enqueued (0, 1 or 2), so g_tx_enq matches what the
  // ISR later transmits.
  unsigned
  txq_putc_locked (char c)
  {
    unsigned n = 0;
    if (c == '\n')
      {
        std::uint32_t next = (g_txq_head + 1) & (TXQ_SIZE - 1);
        if (next == g_txq_tail)
          {
            return 0;
          }
        g_txq[g_txq_head] = '\r';
        g_txq_head = next;
        n = 1;
      }
    std::uint32_t next = (g_txq_head + 1) & (TXQ_SIZE - 1);
    if (next == g_txq_tail)
      {
        return n; // the '\r' went in but the char itself did not
      }
    g_txq[g_txq_head] = c;
    g_txq_head = next;
    return n + 1;
  }

  void
  txq_kick_locked (void)
  {
    rp2350::reg (rp2350::UART0_BASE + IMSC) |= IMSC_TXIM;
  }

  void
  txq_write (const char* s, std::size_t n)
  {
    if (!g_tx_active)
      {
        polled_put_str (s, n);
        return;
      }
    std::uint32_t saved = txq_lock_acquire ();
    for (std::size_t i = 0; i < n; ++i)
      {
        unsigned placed = txq_putc_locked (s[i]);
        g_tx_enq = g_tx_enq + placed;
        if (placed == 0)
          {
            g_tx_dropped = g_tx_dropped + 1;
          }
      }
    txq_kick_locked ();
    txq_lock_release (saved);
  }

  void
  txq_write (const char* s)
  {
    std::size_t n = 0;
    while (s[n] != '\0')
      {
        ++n;
      }
    txq_write (s, n);
  }
}

// The uart namespace API the shared BSP/kernel glue expects (rtos-glue.cpp,
// trace-uart.cpp). uart::init() is called by the early constructor; before
// g_tx_active the writes are polled, after they are interrupt-driven.
namespace uart
{
  void
  init (void)
  {
    unreset_block (RESET_UART0 | RESET_IO_BANK0 | RESET_PADS_BANK0);

    // GPIO0 = TX, GPIO1 = RX (function 2). CTRL is at IO_BANK0 + pin*8 + 4.
    rp2350::reg (rp2350::IO_BANK0_BASE + 0x04)
        = (rp2350::reg (rp2350::IO_BANK0_BASE + 0x04) & ~0x1FU)
          | GPIO_FUNC_UART0;
    rp2350::reg (rp2350::IO_BANK0_BASE + 0x0C)
        = (rp2350::reg (rp2350::IO_BANK0_BASE + 0x0C) & ~0x1FU)
          | GPIO_FUNC_UART0;

    // Un-isolate the pads (PADS reg for pin n is at 0x04 + n*4).
    rp2350::reg (rp2350::PADS_BANK0_BASE + 0x04)
        = (rp2350::reg (rp2350::PADS_BANK0_BASE + 0x04) & ~PADS_ISO) | PADS_IE;
    rp2350::reg (rp2350::PADS_BANK0_BASE + 0x08)
        = (rp2350::reg (rp2350::PADS_BANK0_BASE + 0x08) & ~PADS_ISO) | PADS_IE;

    const std::uint32_t u = rp2350::UART0_BASE;
    rp2350::reg (u + CR) = 0;               // disable while configuring
    rp2350::reg (u + ICR) = 0x7FF;          // clear pending
    rp2350::reg (u + IBRD) = 81;            // 150 MHz / (16 * 115200) = 81.38
    rp2350::reg (u + FBRD) = 24;
    rp2350::reg (u + LCRH) = (3U << 5) | (1U << 4); // 8N1, FIFO enable
    rp2350::reg (u + IFLS) = IFLS_TXIFLSEL_4_8;     // TX IRQ at <= 8 bytes
    rp2350::reg (u + IMSC) = 0;             // no interrupts yet (polled boot)
    rp2350::reg (u + CR) = (1U << 9) | (1U << 8) | (1U << 0); // RXE|TXE|UARTEN
  }

  void
  put_char (char c)
  {
    if (!g_tx_active)
      {
        polled_put_char (c);
        return;
      }
    char s[2] = { c, '\0' };
    txq_write (s, 1);
  }

  void
  write (const char* s)
  {
    txq_write (s);
  }

  void
  write (const char* s, std::size_t n)
  {
    txq_write (s, n);
  }

  void
  print_hex32 (unsigned long v)
  {
    char b[12];
    b[0] = '0';
    b[1] = 'x';
    for (int i = 0; i < 8; ++i)
      {
        unsigned d = (v >> (28 - 4 * i)) & 0xF;
        b[2 + i] = static_cast<char> (d < 10 ? '0' + d : 'a' + d - 10);
      }
    b[10] = '\0';
    write (b);
  }
}

// ----------------------------------------------------------------------------
// UART0 IRQ handler: drains the TX ring into the PL011 FIFO. Runs on CORE 1
// (the only core with the IRQ enabled). No RTOS calls.
// ----------------------------------------------------------------------------
extern "C" void __attribute__ ((section (".after_vectors")))
UART0_IRQ_Handler (void)
{
  g_tx_irq_count = g_tx_irq_count + 1;
  g_isr_core = rp2350::cpu_id ();

  std::uint32_t saved = txq_lock_acquire ();

  bool more = false;
  while (g_txq_tail != g_txq_head)
    {
      if (rp2350::reg (rp2350::UART0_BASE + FR) & FR_TXFF)
        {
          more = true; // FIFO full; leave TXIM set for the next IRQ
          break;
        }
      rp2350::reg (rp2350::UART0_BASE + DR) = static_cast<std::uint32_t> (
          static_cast<unsigned char> (g_txq[g_txq_tail]));
      g_txq_tail = (g_txq_tail + 1) & (TXQ_SIZE - 1);
      g_tx_bytes = g_tx_bytes + 1;
    }

  if (!more)
    {
      // Ring empty: stop the level-triggered TX interrupt.
      rp2350::reg (rp2350::UART0_BASE + IMSC) &= ~IMSC_TXIM;
    }

  txq_lock_release (saved);
}

// ----------------------------------------------------------------------------
// Test threads.
// ----------------------------------------------------------------------------
namespace
{
  semaphore_counting sem_uart_ready{ "u_rdy", 4, 0 };
  constexpr std::uint32_t LINES = 1000;
  constexpr std::uint32_t DRAIN_STALL_LIMIT = 100; // ticks before we call a stall
  volatile bool g_stall_fail = false;

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

  // ---- Core-1: enable the UART IRQ here (cross-core routing), then idle ----
  void*
  irq_host (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 1);
    if (port_cpu_id () != 1)
      {
        this_thread::yield ();
      }

    NVIC_SetPriority (UART0_IRQn, 8);
    NVIC_EnableIRQ (UART0_IRQn);
    g_tx_active = true;
    sem_uart_ready.post ();

    for (;;)
      {
        sysclock.sleep_for (1000);
      }
    return nullptr;
  }

  // ---- Core-0: produce lines through the interrupt-driven ring ------------
  void*
  producer (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 0);
    if (port_cpu_id () != 0)
      {
        this_thread::yield ();
      }

    sem_uart_ready.wait ();

    for (std::uint32_t n = 1; n <= LINES; ++n)
      {
        // Self-throttle to the 115200 wire rate: wait until the ring has
        // fully drained before producing the next line. The producer can
        // never outrun the transmitter, so the ring cannot overflow (a fixed
        // sleep would drift and overflow, dropping bytes on the wire). A
        // stalled ISR visibly stalls the producer and trips g_stall_fail.
        std::uint32_t stall = 0;
        while (g_txq_head != g_txq_tail)
          {
            sysclock.sleep_for (1);
            stall = stall + 1;
            if (stall > DRAIN_STALL_LIMIT)
              {
                g_stall_fail = true;
                break;
              }
          }

        char b[64];
        int len = std::snprintf (
            b, sizeof (b), "line %u: isr=core%u irqs=%u tx=%u enq=%u\n", n,
            static_cast<unsigned> (g_isr_core),
            static_cast<unsigned> (g_tx_irq_count),
            static_cast<unsigned> (g_tx_bytes),
            static_cast<unsigned> (g_tx_enq));
        txq_write (b, static_cast<std::size_t> (len));
        if ((n % 100) == 0)
          {
            led_toggle ();
          }
      }

    // Wait for the final line to drain so enqueued == transmitted at the
    // summary snapshot.
    while (g_txq_head != g_txq_tail)
      {
        sysclock.sleep_for (1);
      }

    bool ok = (g_isr_core == 1) && (g_tx_bytes == g_tx_enq)
              && (g_tx_dropped == 0) && (g_tx_irq_count > 0)
              && !g_stall_fail;
    char b[320];
    int len = std::snprintf (
        b, sizeof (b),
        "---------------------------------------------\n"
        "interrupt-driven UART TX summary\n"
        "  lines       = %u\n"
        "  isr core    = %u (expect 1)\n"
        "  tx irqs     = %u\n"
        "  enqueued    = %u\n"
        "  transmitted = %u\n"
        "  dropped     = %u (expect 0)\n"
        "  stall fail  = %u (expect 0)\n"
        "RESULT: %s\n",
        LINES, static_cast<unsigned> (g_isr_core),
        static_cast<unsigned> (g_tx_irq_count),
        static_cast<unsigned> (g_tx_enq),
        static_cast<unsigned> (g_tx_bytes),
        static_cast<unsigned> (g_tx_dropped),
        static_cast<unsigned> (g_stall_fail ? 1u : 0u),
        ok ? "PASS (cross-core IRQ-driven TX)" : "FAIL");
    txq_write (b, static_cast<std::size_t> (len));

    for (;;)
      {
        sysclock.sleep_for (200);
        led_toggle ();
      }
    return nullptr;
  }

  // Core-1 idle thread (as in smp-test2/3/4).
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

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico 2 / RP2350 - micro-os-plus-iii\n");
  uart::write ("  smp-test5 : interrupt-driven UART TX\n");
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

  uart::write ("core 0: idle1 installed; creating producer/irq_host...\n");

  thread::attributes attr = thread::initializer;

  static thread::stack::element_t prod_stack[1024];
  static thread::stack::element_t host_stack[1024];
  attr.th_stack_address = prod_stack;
  attr.th_stack_size_bytes = sizeof (prod_stack);
  static thread producer_th{ "producer", producer, nullptr, attr };
  producer_th.cpu_affinity (1u << 0);

  attr.th_stack_address = host_stack;
  attr.th_stack_size_bytes = sizeof (host_stack);
  static thread irq_host_th{ "irq_host", irq_host, nullptr, attr };
  irq_host_th.cpu_affinity (1u << 1);

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
