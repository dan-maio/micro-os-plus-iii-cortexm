/*
 * usb_cdc.h — application API for the pico2 USB CDC-ACM (virtual serial)
 * driver, with line-buffered input and block-buffered output. See usb_cdc.c.
 *
 * Buffer sizes are compile-time and independent; override on the command line
 * or before including this header:
 *     -DMU_CDC_RX_BUFSIZE=8192   (input line buffer, default 4096)
 *     -DMU_CDC_TX_BUFSIZE=2048   (output buffer,      default 4096)
 */
#ifndef MU_USB_CDC_H_
#define MU_USB_CDC_H_

#include <stdint.h>

#ifndef MU_CDC_RX_BUFSIZE
#define MU_CDC_RX_BUFSIZE 4096u
#endif
#ifndef MU_CDC_TX_BUFSIZE
#define MU_CDC_TX_BUFSIZE 4096u
#endif
/* MU_CDC_TX_BUFSIZE drives the TX ring AND the tx_space counting semaphore,
   whose count type (os_semaphore_count_t) is int16_t — keep it <= 32767.
   The writes are blocking/flow-controlled, so the ring only needs to buffer
   ahead of the USB drainer; a value of 4096..16384 is plenty. */

#ifdef __cplusplus
extern "C"
{
#endif

  // Construct the driver's cross-core TX mutex. Call once, early in os_main
  // (before any CDC write / before the USB and app threads are created).
  void mu_usb_cdc_init (void);

  // ---- status ----
  int mu_usb_cdc_mounted (void);   // bus enumerated
  int mu_usb_cdc_connected (void); // host opened the port (DTR)

  // ---- input (line-buffered / canonical) ----
  // Characters from the host are buffered and echoed as they arrive; a line is
  // completed on Return (CR or LF) or when the RX buffer is full.
  //
  // mu_usb_cdc_line_ready(): non-zero if a completed line is waiting.
  // mu_usb_cdc_readline():   copy the completed line (without CR/LF, NUL-
  //   terminated) into dst (up to max bytes incl. the NUL) and return its
  //   length; returns -1 if no line is ready.
  int mu_usb_cdc_line_ready (void);
  int mu_usb_cdc_readline (char* dst, uint32_t max);

  // ---- output (block-buffered) ----
  // Bytes are appended to the TX buffer and sent to USB only when the buffer
  // fills or mu_usb_cdc_flush() is called. Returns bytes accepted.
  uint32_t mu_usb_cdc_write (const void* buf, uint32_t len);
  uint32_t mu_usb_cdc_write_str (const char* s);
  void mu_usb_cdc_flush (void); // request the buffered TX be sent to the host

  // Trace logging through the CDC port: "[cdc] …\r\n", flushed immediately.
  void mu_usb_cdc_log (const char* msg);
  void mu_usb_cdc_logf (const char* fmt, ...);

  // ---- USB-task pump ----
  // Call once per tud_task() loop, on the USB task's core. Sends the buffered
  // TX to the endpoint when a flush is pending or the buffer is full.
  void mu_usb_cdc_task (void);

#ifdef __cplusplus
}
#endif

#endif /* MU_USB_CDC_H_ */
