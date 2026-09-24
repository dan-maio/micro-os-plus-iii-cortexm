/*
 * smp-test-usb-hid (Pico 2 / RP2350) — USB-HID keyboard forwarding on the
 * 2-core SMP kernel, using ONLY the µOS++ C++ API (os::rtos) — no std:: layer.
 *
 * Same behaviour as micro-os-plus-iii-stdcpp-pico2/usb-hid-test, but with
 * os::rtos::thread / this_thread / sysclock instead of std::thread, so it needs
 * neither the stdcpp gthreads layer nor exceptions/RTTI.
 *
 *   - core 1 launched (SMP kernel);
 *   - a µOS++ thread pinned to CORE 0 runs the USB device task
 *     (clocks::usb_init(); tusb_init(); loop tud_task()) — pinned so the
 *     USBCTRL IRQ, the HID callback and the PRIMASK critical section share one
 *     core;
 *   - a second µOS++ thread drains received keystrokes and prints them on UART;
 *   - the LED blinks as a heartbeat.
 *
 * Host side: forward keys with the same tools as usb-hid-test
 * (host/kbd_forward.c or forward_kbd.py). Enumerates as Cafe:4012.
 */

#include <cmsis-plus/rtos/os.h>

#include <bsp/led.hpp>
#include <bsp/uart.hpp>
#include <bsp/multicore.hpp>
#include <bsp/clocks.hpp>

#include "tusb.h" // tusb_init() (macro) + tud_task() (inline)

#include <hw_result.hpp>

#include <cstdio>
#include <cstdint>

using namespace os::rtos;

extern "C"
{
  int mu_usb_hid_getchar (void);
  int mu_usb_hid_mounted (void);
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
  // USB device task — pinned to core 0 (see os_main).
  void*
  usb_device_task (void*)
  {
    clocks::usb_init (); // 48 MHz USB clock (PLL_USB) — before tusb_init()
    tusb_init ();
    for (;;)
      {
        tud_task ();
        this_thread::yield ();
      }
    return nullptr;
  }

  // Consumer — drains received keystrokes to UART (µOS++ thread, any core).
  void*
  consumer_task (void*)
  {
    bool was_mounted = false;
    for (;;)
      {
        bool m = mu_usb_hid_mounted () != 0;
        if (m != was_mounted)
          {
            uart::write (m ? "[usb] mounted\n" : "[usb] unmounted\n");
            was_mounted = m;
          }
        int c = mu_usb_hid_getchar ();
        if (c >= 0)
          {
            if (c == 0x1B) // Escape: the host ended the session
              {
                // The HID path is proven end to end; report the verdict and
                // end the run through the semihosting SYS_EXIT.
                uart::write ("\nRESULT: PASS (host sent Escape)\n");
                hw_result::ok ();
              }
            char b[48];
            int n;
            if (c >= 32 && c < 127)
              n = std::snprintf (b, sizeof (b), "key: '%c' (0x%02x)\n",
                                 (char) c, (unsigned) c);
            else
              n = std::snprintf (b, sizeof (b), "key: 0x%02x\n", (unsigned) c);
            uart::write (b, (std::size_t) n);
          }
        else
          {
            sysclock.sleep_for (2);
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
  thread::stack::element_t idle1_stack[512];
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  this_thread::thread ().cpu_affinity (1u << 0); // setup on core 0

  led::init ();

  uart::write ("\n=============================================\n");
  uart::write ("  Pico 2 / RP2350 - micro-os-plus-iii\n");
  uart::write ("  smp-test-usb-hid : host kbd -> USB HID -> UART\n");
  uart::write ("  (µOS++ C++ API only, no std:: layer)\n");
  uart::write ("=============================================\n");

  thread::attributes attr = thread::initializer;

  // Launch core 1 before creating app threads (SMP launch-order rule).
  attr.th_stack_address = idle1_stack;
  attr.th_stack_size_bytes = sizeof (idle1_stack);
  static thread idle1{ "idle1", secondary_idle_func, nullptr, attr };
  os::rtos::scheduler::os_idle_thread_core[1] = &idle1;

  uart::write ("core 0: launching core 1...\n");
  multicore::launch_core1 (port_smp_secondary_start, port_core1_stack_top ());
  port_smp_core0_ipi_enable ();

  // USB device task — pinned to core 0.
  attr.th_stack_address = usb_stack;
  attr.th_stack_size_bytes = sizeof (usb_stack);
  static thread usb_th{ "usb", usb_device_task, nullptr, attr };
  usb_th.cpu_affinity (1u << 0);

  // Consumer thread.
  attr.th_stack_address = cons_stack;
  attr.th_stack_size_bytes = sizeof (cons_stack);
  static thread cons_th{ "cons", consumer_task, nullptr, attr };

  uart::write ("core 0: USB HID device up; run host/kbd_forward and type.\n");

  // Keep os_main alive. The test is driven by the host over USB HID: the
  // consumer thread reports RESULT and ends the run when the host presses
  // Escape (see consumer_task). It does not finish on its own.
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
  return 0;
}
