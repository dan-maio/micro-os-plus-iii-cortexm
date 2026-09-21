/*
 * uart-test1 — USART1 echo test for micro-os-plus-iii on the WeAct F412RE.
 *
 * Ports the Eclipse `f412re` application to the shared µOS++ kernel: the
 * USART1 driver keeps its RXNE receive interrupt and its DMA2 Stream7
 * transmit engine, and the application is split into three RTOS threads:
 *
 *   - rx  : blocks on the RX interrupt, hands each received byte to a bounded
 *           buffer (producer);
 *   - tx  : drains the bounded buffer to the UART through the TX DMA
 *           (consumer), and emits a heartbeat when idle for a second;
 *   - led : blinks PB2 at 1 Hz (with `led on`/`led off` on semihosting).
 *
 * The bounded buffer is guarded by a plain mutex and the usual free/used
 * counting-semaphore pair, exactly like mos-test1.
 */

#include <cmsis-plus/rtos/os.h>
#include <cmsis-plus/diag/trace.h>

#include <board.hpp>
#include <led.hpp>
#include <uart.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace os::rtos;

namespace
{
  constexpr int BUFFER_SIZE = 32;
  constexpr unsigned LED_PERIOD_MS = 500;   // half period -> 1 Hz blink
  constexpr std::uint32_t TX_IDLE_MS = 1000; // heartbeat period

  std::uint8_t g_buf[BUFFER_SIZE];
  int g_head = 0;
  int g_tail = 0;

  mutex g_buf_mutex{ "uart-buf" };
  semaphore_counting g_buf_free{ "buf-free", BUFFER_SIZE, BUFFER_SIZE };
  semaphore_counting g_buf_used{ "buf-used", BUFFER_SIZE, 0 };

  thread::stack::element_t rx_stack[1024];
  thread::stack::element_t tx_stack[1024];
  thread::stack::element_t led_stack[1024];

  // ----- rx thread ---------------------------------------------------------

  void*
  rx_func (void* /*arg*/)
  {
    for (;;)
      {
        const int c = uart::read_char (); // blocks until the RX interrupt fires
        if (c < 0)
          {
            continue;
          }

        g_buf_free.wait (); // wait for a free slot
        g_buf_mutex.lock ();
        g_buf[g_head] = static_cast<std::uint8_t> (c);
        g_head = (g_head + 1) % BUFFER_SIZE;
        g_buf_mutex.unlock ();
        g_buf_used.post (); // signal an item is available
      }
    return nullptr;
  }

  // ----- tx thread ---------------------------------------------------------

  void*
  tx_func (void* /*arg*/)
  {
    unsigned heartbeat = 0;
    for (;;)
      {
        if (g_buf_used.timed_wait (TX_IDLE_MS) == result::ok)
          {
            g_buf_mutex.lock ();
            const std::uint8_t b = g_buf[g_tail];
            g_tail = (g_tail + 1) % BUFFER_SIZE;
            g_buf_mutex.unlock ();
            g_buf_free.post ();

            uart::put_char (static_cast<char> (b)); // DMA
          }
        else
          {
            char line[48];
            const int n = std::snprintf (line, sizeof (line),
                                         "[tx] heartbeat %u\n", heartbeat++);
            if (n > 0)
              {
                uart::write (line, static_cast<std::size_t> (n));
              }
          }
      }
    return nullptr;
  }

  // ----- led thread --------------------------------------------------------

  void*
  led_func (void* /*arg*/)
  {
    for (;;)
      {
        led::on ();
        os::trace::printf ("led on\n");
        sysclock.sleep_for (LED_PERIOD_MS);

        led::off ();
        os::trace::printf ("led off\n");
        sysclock.sleep_for (LED_PERIOD_MS);
      }
    return nullptr;
  }
}

// ----------------------------------------------------------------------------

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::init (board::UART_BAUD);

  uart::write ("uart-test1: micro-os-plus-iii threads + USART1 IRQ RX + DMA TX\n");
  uart::write ("threads: led blink, uart rx, uart tx (echo + heartbeat)\n");

  thread::attributes attr = thread::initializer;

  attr.th_stack_address = rx_stack;
  attr.th_stack_size_bytes = sizeof (rx_stack);
  static thread rx_thread{ "rx", rx_func, nullptr, attr };

  attr.th_stack_address = tx_stack;
  attr.th_stack_size_bytes = sizeof (tx_stack);
  static thread tx_thread{ "tx", tx_func, nullptr, attr };

  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_thread{ "led", led_func, nullptr, attr };

  // os_main has nothing more to do; keep it alive so the workers run.
  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
