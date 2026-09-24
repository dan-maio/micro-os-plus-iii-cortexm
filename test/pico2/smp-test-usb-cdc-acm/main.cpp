/*
 * smp-test-usb-cdc-acm (Pico 2 / RP2350) — USB CDC-ACM (virtual serial) I/O
 * with the host on the 2-core SMP kernel, using ONLY the µOS++ C++ API
 * (os::rtos) — no std:: layer.
 *
 * The board enumerates as a USB CDC-ACM device, so the host sees a virtual
 * serial port (/dev/ttyACM*, COMx). Anything you type in a host terminal is
 * received by the device, echoed back over the same CDC port, and mirrored on
 * the hardware UART. The CDC driver also emits its own trace ("[cdc] ...")
 * over the same virtual serial port (see src/usb-cdc/usb_cdc.c).
 *
 *   - core 1 launched (SMP kernel);
 *   - a µOS++ thread pinned to CORE 0 runs the USB device task
 *     (clocks::usb_init(); tusb_init(); loop { tud_task(); mu_usb_cdc_task(); })
 *     — pinned so the USBCTRL IRQ, the CDC callbacks and the TX drain share one
 *     core;
 *   - a second µOS++ thread is a full bidirectional bridge:
 *       * CDC (host) -> UART console  (+ echoed back to the host);
 *       * UART console -> CDC (host)  (+ locally echoed on the console);
 *   - the LED blinks as a heartbeat.
 *
 * Two terminals: your existing UART0 console @115200 8N1 (GPIO0/1), and the USB
 * virtual serial `screen /dev/ttyACM<n> 115200`. Type in either and it appears on
 * the other. Enumerates as Cafe:4013 -- a NEW /dev/ttyACM*, not necessarily
 * ttyACM0: a debug probe's own UART bridge is often ttyACM0 already.
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/uart.hpp>
#include <bsp/multicore.hpp>
#include <bsp/clocks.hpp>

#include <bsp/ws2812.hpp> // onboard RGB LED (RP2350B-MINI-A)

#include <hw_result.hpp>

#include "tusb.h"     // tusb_init() (macro) + tud_task() (inline)
#include "usb_cdc.h"  // mu_usb_cdc_* driver API

#include <cstdio>
#include <cstdint>

using namespace os::rtos;

extern "C"
{
  void port_smp_secondary_start (void);
  unsigned port_core1_stack_top (void);
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
  // USB bring-up stage, read by blink_task for LED diagnostics (no UART here):
  //   0 = boot / before usb_init        1 = usb_init running (PLL_USB lock)
  //   2 = usb_init done                 3 = tusb_init done
  //   4 = host mounted the device
  volatile int g_usb_stage = 0;

  // Current LED colour driven by blink_task (core 1); 0 = off, 1..3 = R/G/B.
  // This is the ONLY cross-core CDC-related state: it is a plain volatile int
  // (atomic 32-bit write), so it needs no lock. The USB task (core 0) reads it
  // and emits the "[cdc] led ON/OFF" logs — keeping every CDC ring producer on
  // core 0, so the ring itself needs no cross-core lock.
  volatile int g_led_state = 0;

  // Log "label=0xVALUE" over CDC without printf/vsnprintf (which corrupts state
  // here). Fixed 8-digit hex.
  void
  log_hex (const char* label, std::uint32_t v)
  {
    char b[40];
    std::size_t p = 0;
    while (*label && p < 24)
      b[p++] = *label++;
    b[p++] = '=';
    b[p++] = '0';
    b[p++] = 'x';
    for (int s = 28; s >= 0; s -= 4)
      {
        unsigned d = (v >> s) & 0xF;
        b[p++] = (char) (d < 10 ? '0' + d : 'a' + d - 10);
      }
    b[p] = '\0';
    mu_usb_cdc_log (b);
  }

  // USB device task — pinned to core 0 (see os_main). It is the only task that
  // writes to the CDC TX ring besides the console task (also core 0); the blink
  // task's "led ON/OFF" messages are emitted here from g_led_state.
  void*
  usb_device_task (void*)
  {
    g_usb_stage = 1;
    clocks::usb_init (); // 48 MHz USB clock (PLL_USB) — before tusb_init()
    g_usb_stage = 2;
    tusb_init ();
    g_usb_stage = 3;
    int prev_led = 0;
    for (;;)
      {
        tud_task ();        // service the USB device stack
        mu_usb_cdc_task (); // drain the CDC TX ring onto the bulk IN endpoint
        if (tud_mounted ())
          g_usb_stage = 4;

        // Emit the blink task's LED transitions over CDC (core 0 only).
        int ls = g_led_state;
        if (ls != prev_led)
          {
            prev_led = ls;
            if (ls == 0)
              mu_usb_cdc_log ("led OFF");
            else if (ls == 1)
              mu_usb_cdc_log ("led ON red");
            else if (ls == 2)
              mu_usb_cdc_log ("led ON green");
            else
              mu_usb_cdc_log ("led ON blue");

            // Longer TX-path exercise: 80 chars as two 40-char lines, sent
            // once per LED-on transition.
            if (ls != 0)
              {
                static const char long_line1[]
                    = "0123456789ABCDEFGHIJ0123456789ABCDEFGHIJ"; // 40
                static const char long_line2[]
                    = "9876543210JIHGFEDCBA9876543210JIHGFEDCBA"; // 40
                mu_usb_cdc_write_str (long_line1);
                mu_usb_cdc_write_str ("\r\n");
                mu_usb_cdc_write_str (long_line2);
                mu_usb_cdc_write_str ("\r\n");
                mu_usb_cdc_flush ();
              }
          }
        this_thread::yield ();
      }
    return nullptr;
  }

  // Console consumer — line-buffered. The CDC driver echoes keystrokes and
  // assembles a line (completed on Return or when its 4K buffer fills); here we
  // pull each completed line, mirror it on UART, and send a buffered reply back
  // to the host that only reaches the wire when we flush().
  void*
  console_task (void*)
  {
    static char line[MU_CDC_RX_BUFSIZE];
    bool was_mounted = false;
    for (;;)
      {
        bool m = mu_usb_cdc_mounted () != 0;
        if (m != was_mounted)
          {
            uart::write (m ? "[usb] mounted\n" : "[usb] unmounted\n");
            was_mounted = m;
          }

        int n = mu_usb_cdc_readline (line, sizeof (line));
        if (n >= 0)
          {
            // Escape (0x1B) in a received line ends the host-driven session.
            for (int i = 0; i < n; ++i)
              {
                if (static_cast<unsigned char> (line[i]) == 0x1B)
                  {
                    uart::write ("\nRESULT: PASS (host sent Escape)\n");
                    hw_result::ok ();
                  }
              }

            // Local mirror on the UART console.
            uart::write ("[line] ");
            uart::write (line, (std::size_t) n);
            uart::write ("\n");

            // Buffered reply to the host: nothing is transmitted until flush().
            mu_usb_cdc_write_str ("you typed: ");
            mu_usb_cdc_write (line, (std::uint32_t) n);
            mu_usb_cdc_write_str ("\r\n");
            mu_usb_cdc_flush (); // now it goes out
          }
        else
          {
            sysclock.sleep_for (5);
          }
      }
    return nullptr;
  }

  // Heartbeat — its own µOS++ thread, pinned to core 1. While the USB device is
  // NOT mounted it shows the USB bring-up stage as a solid color (LED diag,
  // since there is no UART on this board):
  //   red    = stuck in clocks::usb_init (PLL_USB never locked)
  //   orange = usb_init done, stuck in tusb_init
  //   blue   = tusb_init done, device not (yet) mounted by the host
  // Once mounted: R/G/B heartbeat. The blink task sets g_led_state only; the
  // "led ON/OFF" CDC logs are emitted by the core-0 USB task (no cross-core
  // ring writes).
  void*
  blink_task (void*)
  {
    ws2812::init ();
    ws2812::set (0, 0, 0); // off
    g_led_state = 0;
    unsigned k = 0;
    for (;;)
      {
        int st = g_usb_stage;
        if (st >= 4)
          {
            static const struct { std::uint8_t r, g, b; int c; } beat[] = {
              { 40, 0, 0, 1 }, { 0, 40, 0, 2 }, { 0, 0, 40, 3 }
            };
            const auto& c = beat[k++ % 3];
            ws2812::set (c.r, c.g, c.b);
            g_led_state = c.c;
            sysclock.sleep_for (900);
            ws2812::set (0, 0, 0);
            g_led_state = 0;
            sysclock.sleep_for (100);
          }
        else
          {
            struct { std::uint8_t r, g, b; } c;
            switch (st)
              {
              case 0:
              case 1:
                c = { 40, 0, 0 };  break; // red   : stuck in usb_init
              case 2:
                c = { 40, 20, 0 }; break; // orange: stuck in tusb_init
              default:
                c = { 0, 0, 40 };  break; // blue  : not mounted yet
              }
            ws2812::set (c.r, c.g, c.b);
            sysclock.sleep_for (400);
          }
      }
    return nullptr;
  }

  void*
  secondary_idle_func (void*)
  {
    this_thread::thread ().priority (thread::priority::idle);
    for (;;)
      {
        __asm__ volatile ("dsb" ::: "memory");
        __asm__ volatile ("wfi");
        this_thread::yield ();
      }
    return nullptr;
  }

  thread::stack::element_t usb_stack[4096];
  thread::stack::element_t cons_stack[2048];
  thread::stack::element_t blink_stack[2048];
  thread::stack::element_t idle1_stack[512];
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  this_thread::thread ().cpu_affinity (1u << 0); // setup on core 0

  mu_usb_cdc_init (); // initialise the CDC TX ring (all ring producers on core 0)

  // Banner -> USB CDC. This is queued on the CDC TX ring now (before the USB
  // stack is even up) and flushes to the host terminal the moment it opens
  // its /dev/ttyACM* (DTR asserts). It is NOT sent to the UART console.
  mu_usb_cdc_write_str ("\r\n=============================================\r\n");
  mu_usb_cdc_write_str ("  Pico 2 / RP2350 - micro-os-plus-iii\r\n");
  mu_usb_cdc_write_str ("  smp-test-usb-cdc-acm : USB virtual serial I/O\r\n");
  mu_usb_cdc_write_str ("  (\xC2\xB5OS++ C++ API only, no std:: layer)\r\n");
  mu_usb_cdc_write_str ("=============================================\r\n");

  // One-shot WS2812 diagnostics, run HERE on core 0 (before the app threads
  // start). selftest() reconfigures/toggles the pad and measure_clk_khz() masks
  // IRQs ~100 us + pokes SysTick — fine as a one-time core-0 bring-up call, but
  // NOT from the core-1 blink thread (doing so destabilised USB).
  //   selftest: bit0=IN@high(1) bit1=IN@low(0) bit2=OE(1) [12:8]=funcsel(5)
  //   clk_khz : measured clk_sys (0x000493e0 = 300000)
  log_hex ("gpio20.selftest", ws2812::selftest (ws2812::default_pin ()));
  log_hex ("clk_khz", ws2812::measure_clk_khz ());
  mu_usb_cdc_flush (); // push the buffered banner+diagnostics out on connect

  thread::attributes attr = thread::initializer;

  // Launch core 1 before creating app threads (SMP launch-order rule).
  attr.th_stack_address = idle1_stack;
  attr.th_stack_size_bytes = sizeof (idle1_stack);
  static thread idle1{ "idle1", secondary_idle_func, nullptr, attr };
  os::rtos::scheduler::os_idle_thread_core[1] = &idle1;

  uart::write ("core 0: launching core 1...\n");
  multicore::launch_core1 (port_smp_secondary_start, port_core1_stack_top ());
  port_smp_core0_ipi_enable ();

  // Every thread below is pinned AT CONSTRUCTION (th_cpu_affinity), not by a
  // cpu_affinity() call after it: core 1 is already running, and it can pick
  // a new thread off the ready list before a later pin lands. The USB thread
  // must start on core 0, because tusb_init() enables USBCTRL_IRQ in the NVIC
  // of the core it runs on (each RP2350 core has its own); started on core 1,
  // the interrupt is serviced there while tud_task() runs on core 0, and the
  // device never answers the host's setup requests.

  // USB device task — pinned to core 0.
  attr.th_stack_address = usb_stack;
  attr.th_stack_size_bytes = sizeof (usb_stack);
  attr.th_cpu_affinity = (1u << 0);
  static thread usb_th{ "usb", usb_device_task, nullptr, attr };

  // Console consumer thread (line-buffered CDC input). PINNED TO CORE 0: it is
  // a CDC TX ring producer, and every ring producer must stay on the USB core
  // so the ring only needs a PRIMASK guard (no cross-core lock).
  attr.th_stack_address = cons_stack;
  attr.th_stack_size_bytes = sizeof (cons_stack);
  attr.th_cpu_affinity = (1u << 0);
  static thread console_th{ "console", console_task, nullptr, attr };

  // Heartbeat — its own thread (drives the onboard WS2812 RGB LED). PINNED TO
  // CORE 1: the WS2812 frame masks IRQs (cpsid i) for ~35 µs; keeping it off
  // core 0 leaves the USB device task's core undisturbed.
  attr.th_stack_address = blink_stack;
  attr.th_stack_size_bytes = sizeof (blink_stack);
  attr.th_cpu_affinity = (1u << 1);
  static thread blink_th{ "blink", blink_task, nullptr, attr };

  uart::write ("core 0: CDC-ACM up. Open the gadget's port (Cafe:4013, a NEW\n");
  uart::write ("        /dev/ttyACM*; not the probe's UART), type a line,\n");
  uart::write ("        press Return; the line is echoed and mirrored here.\n");
  uart::write ("        send Escape to finish.\n");

  // Keep os_main alive; the test is driven by the host over the CDC. The
  // console thread reports RESULT and ends the run when the host sends Escape
  // (see console_task).
  for (;;)
    sysclock.sleep_for (1000);
  return 0;
}
