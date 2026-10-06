/*
 * usb_cdc.c — CDC-ACM (virtual serial) driver with line-buffered input and
 * block-buffered output for the pico2 USB device. See usb_cdc.h.
 *
 * INPUT (canonical / line-buffered): bytes from the host accumulate in a line
 *   buffer (MU_CDC_RX_BUFSIZE) and are echoed as they arrive (with backspace
 *   handling). A line is completed on Return (CR or LF) or when the buffer
 *   fills, then handed to the application via mu_usb_cdc_readline().
 *
 * OUTPUT (block-buffered): mu_usb_cdc_write() appends to a TX buffer
 *   (MU_CDC_TX_BUFSIZE); it is sent to USB only when that buffer fills or
 *   mu_usb_cdc_flush() is called. The USB task (mu_usb_cdc_task(), on the USB
 *   core) is the only place that touches the non-thread-safe tud_cdc_* API.
 *
 *   The ring is NON-BLOCKING: if it is full, the append drops bytes. Long
 *   output is kept lossless by the CALLER pacing the print (a small sleep per
 *   line so the drainer keeps up) — see the app's print loops. No kernel
 *   semaphore is used here: the blocking/semaphore experiments deadlocked and
 *   faulted on this stack, while non-blocking + pacing is simple and reliable.
 *
 * The USB device task (tusb_init + loop { tud_task(); mu_usb_cdc_task(); })
 * lives in the application, on a µOS++ thread pinned to core 0.
 */

#include "tusb.h"
#include "usb_cdc.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define RX_CAP (MU_CDC_RX_BUFSIZE)
#define TX_CAP (MU_CDC_TX_BUFSIZE)

// Serialisation of the TX ring: PRIMASK only. ALL ring producers run on the
// USB core (core 0): os_main banner/solver output, the USB task's rx echo, and
// the console task (pinned to core 0). No cross-core writes are allowed.
// PRIMASK (cpsid i) blocks preemption on core 0 while appending; the USB IRQ
// may stay pending during the append but never touches the ring in ISR context.
void
mu_usb_cdc_init (void)
{
}

static inline uint32_t
tx_lock (void)
{
  uint32_t pm;
  __asm__ volatile ("mrs %0, primask" : "=r"(pm));
  __asm__ volatile ("cpsid i" ::: "memory");
  return pm;
}

static inline void
tx_unlock (uint32_t pm)
{
  __asm__ volatile ("msr primask, %0" : : "r"(pm) : "memory");
}

// ---- input: current line + one completed-line slot ------------------------
static char rx_line[RX_CAP];           // line being assembled (USB core only)
static uint32_t rx_idx;                 // chars in rx_line
static char rx_ready[RX_CAP];           // last completed line
static volatile uint32_t rx_ready_len;  // its length (valid when line_ready)
static volatile int rx_line_ready;      // a completed line is waiting

// ---- output: byte ring drained only on flush/full -------------------------
static uint8_t tx_buf[TX_CAP];
static volatile uint32_t tx_head;       // producer(s), PRIMASK-guarded
static volatile uint32_t tx_tail;       // consumer (USB core)
static volatile int tx_flush;           // send request pending

// Append bytes to the TX buffer, NON-BLOCKING: drops once the ring is full
// (the caller paces long output so the drainer keeps up). Returns the number
// of bytes accepted.
static uint32_t
tx_append (const uint8_t* p, uint32_t n)
{
  uint32_t done = 0;
  uint32_t pm = tx_lock ();
  for (uint32_t i = 0; i < n; ++i)
    {
      uint32_t nh = (tx_head + 1u) % TX_CAP;
      if (nh == tx_tail)
        {
          tx_flush = 1; // full -> must drain
          break;
        }
      tx_buf[tx_head] = p[i];
      tx_head = nh;
      ++done;
    }
  if (tx_head != tx_tail)
    tx_flush = 1; // something to send
  tx_unlock (pm);
  return done;
}

// ---- status ----------------------------------------------------------------

int
mu_usb_cdc_mounted (void)
{
  return tud_mounted () ? 1 : 0;
}

int
mu_usb_cdc_connected (void)
{
  return tud_cdc_connected () ? 1 : 0;
}

// ---- output API ------------------------------------------------------------

uint32_t
mu_usb_cdc_write (const void* buf, uint32_t len)
{
  return tx_append ((const uint8_t*) buf, len);
}

uint32_t
mu_usb_cdc_write_str (const char* s)
{
  return tx_append ((const uint8_t*) s, (uint32_t) strlen (s));
}

void
mu_usb_cdc_flush (void)
{
  tx_flush = 1;
}

void
mu_usb_cdc_log (const char* msg)
{
  static const char pfx[] = "[cdc] ";
  tx_append ((const uint8_t*) pfx, sizeof (pfx) - 1);
  tx_append ((const uint8_t*) msg, (uint32_t) strlen (msg));
  tx_append ((const uint8_t*) "\r\n", 2);
  tx_flush = 1;
}

void
mu_usb_cdc_logf (const char* fmt, ...)
{
  char b[96];
  __builtin_va_list ap;
  __builtin_va_start (ap, fmt);
  int n = vsnprintf (b, sizeof (b), fmt, ap);
  __builtin_va_end (ap);
  if (n < 0)
    return;
  if (n > (int) sizeof (b) - 1)
    n = sizeof (b) - 1;
  static const char pfx[] = "[cdc] ";
  tx_append ((const uint8_t*) pfx, sizeof (pfx) - 1);
  tx_append ((const uint8_t*) b, (uint32_t) n);
  tx_append ((const uint8_t*) "\r\n", 2);
  tx_flush = 1;
}

// ---- input API -------------------------------------------------------------

int
mu_usb_cdc_line_ready (void)
{
  return rx_line_ready;
}

int
mu_usb_cdc_readline (char* dst, uint32_t max)
{
  if (!rx_line_ready)
    return -1;
  uint32_t len = rx_ready_len;
  if (max == 0)
    return -1;
  if (len > max - 1u)
    len = max - 1u;
  memcpy (dst, rx_ready, len);
  dst[len] = '\0';
  rx_line_ready = 0; // free the slot for the next line
  return (int) len;
}

// ---- USB-task pump ---------------------------------------------------------

void
mu_usb_cdc_task (void)
{
  if (!tud_cdc_connected ())
    return;

  // Send buffered TX only when a flush is pending (which includes "buffer
   // full", set by the appends). Pop from the ring under the PRIMASK lock into
   // a local chunk, then call the (non-thread-safe, USB-core-only)
   // tud_cdc_write OUTSIDE the lock.
   if (tx_flush)
     {
       for (;;)
         {
           uint32_t avail = tud_cdc_write_available ();
           if (avail == 0)
             break; // endpoint FIFO full — finish next loop, keep tx_flush
           uint8_t chunk[64];
           uint32_t k = 0;
           uint32_t pm = tx_lock ();
           while (k < sizeof (chunk) && k < avail && tx_tail != tx_head)
             {
               chunk[k++] = tx_buf[tx_tail];
               tx_tail = (tx_tail + 1u) % TX_CAP;
             }
           tx_unlock (pm);
           if (k == 0)
             break; // ring empty
           tud_cdc_write (chunk, k);
         }
      tud_cdc_write_flush ();
      uint32_t pm = tx_lock ();
      int empty = (tx_tail == tx_head);
      tx_unlock (pm);
      if (empty)
        tx_flush = 0; // fully drained
    }
}

// ---- TinyUSB CDC callbacks (run on the USB core during tud_task) -----------

// Echo helper: queue bytes and request an immediate flush so typing is visible.
// Runs inside tud_task on the draining core, so it must NOT block.
static void
echo (const char* p, uint32_t n)
{
  tx_append ((const uint8_t*) p, n);
  tx_flush = 1;
}

// Finalize the current line into the completed-line slot.
static void
line_complete (void)
{
  uint32_t n = rx_idx;
  memcpy (rx_ready, rx_line, n);
  rx_ready_len = n;
  rx_line_ready = 1; // (a previously unread line is overwritten)
  rx_idx = 0;
  echo ("\r\n", 2);
}

void
tud_cdc_rx_cb (uint8_t itf)
{
  (void) itf;
  uint8_t tmp[64];
  while (tud_cdc_available ())
    {
      uint32_t n = tud_cdc_read (tmp, sizeof (tmp));
      for (uint32_t i = 0; i < n; ++i)
        {
          char c = (char) tmp[i];
          if (c == '\r' || c == '\n')
            {
              line_complete ();
            }
          else if (c == 0x08 || c == 0x7f) // backspace / DEL
            {
              if (rx_idx > 0)
                {
                  --rx_idx;
                  echo ("\b \b", 3);
                }
            }
          else
            {
              if (rx_idx < RX_CAP - 1u)
                {
                  rx_line[rx_idx++] = c;
                  echo (&c, 1);
                  if (rx_idx >= RX_CAP - 1u)
                    line_complete (); // buffer full -> complete the line
                }
            }
        }
    }
}

void
tud_cdc_line_state_cb (uint8_t itf, bool dtr, bool rts)
{
  (void) itf;
  (void) rts;
  mu_usb_cdc_log (dtr ? "host connected (DTR=1)" : "host disconnected (DTR=0)");
}

void
tud_mount_cb (void)
{
  mu_usb_cdc_log ("mounted");
}

void
tud_umount_cb (void)
{
  mu_usb_cdc_log ("unmounted");
}
