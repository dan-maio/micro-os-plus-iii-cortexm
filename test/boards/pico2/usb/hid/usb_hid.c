/*
 * usb_hid.c — receive glue for the pico2 USB-HID device.
 *
 * The board enumerates as a generic in/out HID device. A host-side forwarder
 * (the test's host/kbd_forward) writes the keys you type into an HID OUT report;
 * TinyUSB delivers them here via tud_hid_set_report_cb(), and we push the bytes
 * into a small lock-free-ish ring the application drains and prints on UART.
 *
 * The USB device task (tusb_init + tud_task loop) lives in the application, on
 * a µOS++ thread pinned to core 0 (so the USBCTRL_IRQ + this callback all run
 * on one core; see the test main.cpp).
 */

#include "tusb.h"
#include <stdint.h>

// ---- received-byte ring (single producer: USB IRQ/task; single consumer) ----
#define RX_CAP 256u
static volatile uint8_t rx_buf[RX_CAP];
static volatile uint32_t rx_head; // producer
static volatile uint32_t rx_tail; // consumer

static inline void
rx_push (uint8_t c)
{
  uint32_t h = rx_head;
  uint32_t n = (h + 1u) % RX_CAP;
  if (n != rx_tail) // drop on overflow
    {
      rx_buf[h] = c;
      rx_head = n;
    }
}

// Application API: -1 if empty, else the next received byte.
int
mu_usb_hid_getchar (void)
{
  if (rx_tail == rx_head)
    return -1;
  uint8_t c = rx_buf[rx_tail];
  rx_tail = (rx_tail + 1u) % RX_CAP;
  return (int) c;
}

int
mu_usb_hid_mounted (void)
{
  return tud_mounted () ? 1 : 0;
}

// ---- TinyUSB HID callbacks --------------------------------------------------

// Host -> device: an OUT report arrived (our forwarded keystrokes). The
// forwarder packs [len][byte0..byteN-1]; if len==0 we treat the whole buffer.
void
tud_hid_set_report_cb (uint8_t itf, uint8_t report_id,
                       hid_report_type_t report_type, uint8_t const* buffer,
                       uint16_t bufsize)
{
  (void) itf;
  (void) report_id;
  (void) report_type;
  if (bufsize == 0)
    return;
  uint16_t n = buffer[0];
  uint16_t start = 1;
  if (n == 0 || n > bufsize - 1)
    {
      n = bufsize; // no length prefix: take the raw buffer, trimming NULs
      start = 0;
    }
  for (uint16_t i = 0; i < n && (start + i) < bufsize; ++i)
    {
      uint8_t c = buffer[start + i];
      if (c != 0)
        rx_push (c);
    }
}

// Device -> host GET_REPORT: unused (we only receive). Return 0 length.
uint16_t
tud_hid_get_report_cb (uint8_t itf, uint8_t report_id,
                       hid_report_type_t report_type, uint8_t* buffer,
                       uint16_t reqlen)
{
  (void) itf;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;
  return 0;
}
