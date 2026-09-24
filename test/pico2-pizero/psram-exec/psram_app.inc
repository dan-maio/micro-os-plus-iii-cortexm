/*
 * psram_app.cpp — a self-contained mini app that lives ENTIRELY in PSRAM (own
 * vector table, code, rodata, data, bss). Compiled TWICE (see the Makefile):
 *
 *   APP A: -DAPP_SEC_STR="a" -DAPP_IS_A=1   -> sections .psram_a_*  @ 0x11000000
 *   APP B: -DAPP_SEC_STR="b" -DAPP_IS_A=0   -> sections .psram_b_*  @ 0x11400000
 *
 * All symbols are file-local (static) so the two objects never clash; only the
 * linker SECTIONS differ, so each app becomes an independent image at its own
 * PSRAM address. The supervisor (main.cpp) copies both into PSRAM and calls the
 * active app's entry (its vector[1]); the entry runs a heartbeat loop until a
 * key arrives on the UART, then RETURNS so the supervisor can switch apps.
 */

#include <cstdint>

#ifndef APP_SEC_STR
#define APP_SEC_STR "a"
#endif
#ifndef APP_IS_A
#define APP_IS_A 1
#endif

// Place a symbol in this app's PSRAM section: ".psram_<id>_<suffix>".
#define SEC(suffix)                                                           \
  __attribute__ ((section (".psram_" APP_SEC_STR suffix), used))

#if APP_IS_A
#define APP_LETTER "A"
#define APP_MAGIC 0x5A5A
#else
#define APP_LETTER "B"
#define APP_MAGIC 0xB00B
#endif

// Set by the supervisor; an app writes 2 here when the host presses 'x'.
extern volatile unsigned g_psram_stop;

namespace
{
  inline volatile std::uint32_t&
  reg (std::uint32_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }
  constexpr std::uint32_t UART0 = 0x40070000U; // RP2350 UART0
  constexpr std::uint32_t UART_DR = 0x00U, UART_FR = 0x18U;
  constexpr std::uint32_t FR_TXFF = 1U << 5, FR_RXFE = 1U << 4;

  constexpr std::uint32_t IO_BANK0 = 0x40028000U, PADS_BANK0 = 0x40038000U;
  constexpr std::uint32_t SIO = 0xD0000000U;
  constexpr std::uint32_t SIO_OUT_SET = 0x018U, SIO_OUT_CLR = 0x020U,
                          SIO_OE_SET = 0x038U;
  constexpr std::uint32_t LED_PIN = 5U, GPIO_FUNC_SIO = 5U;
  constexpr std::uint32_t PADS_ISO = 1U << 8, PADS_IE = 1U << 6;
}

extern char __StackTop[]; // reuse the loader's SRAM stack (unused in call-mode)

// ---- App string data + variables, IN PSRAM ---------------------------------
SEC ("_rodata") static const char MSG_BANNER[]
    = "\r\n----------------------------------------\r\n"
      "  APP " APP_LETTER " is now ACTIVE (from PSRAM)\r\n"
      "  press ESC to switch apps\r\n"
      "----------------------------------------\r\n";
SEC ("_rodata") static const char MSG_HB[] = "  APP " APP_LETTER " heartbeat #";
SEC ("_rodata") static const char MSG_NL[] = "\r\n";

SEC ("_data") static int app_magic = APP_MAGIC; // initialised (.psram_<id>_data)
SEC ("_bss") static int app_counter;            // zero-init (.psram_<id>_bss)

// ---- App functions, IN PSRAM -----------------------------------------------
static SEC ("_text") void
app_putc (char c)
{
  while (reg (UART0 + UART_FR) & FR_TXFF)
    {
    }
  reg (UART0 + UART_DR)
      = static_cast<std::uint32_t> (static_cast<std::uint8_t> (c));
}

static SEC ("_text") void
app_print (const char* s)
{
  while (*s != '\0')
    {
      app_putc (*s++);
    }
}

static SEC ("_text") void
app_print_dec (int v)
{
  char b[12];
  int n = 0;
  unsigned u = static_cast<unsigned> (v);
  do
    {
      b[n++] = static_cast<char> ('0' + (u % 10));
      u /= 10;
    }
  while (u != 0);
  while (n > 0)
    {
      app_putc (b[--n]);
    }
}

static SEC ("_text") bool
rx_pending (void)
{
  return (reg (UART0 + UART_FR) & FR_RXFE) == 0; // RX FIFO not empty
}

static SEC ("_text") void
led_init (void)
{
  volatile std::uint32_t& pad = reg (PADS_BANK0 + 0x04U + LED_PIN * 4U);
  pad = (pad & ~PADS_ISO) | PADS_IE;
  volatile std::uint32_t& ctrl = reg (IO_BANK0 + LED_PIN * 8U + 4U);
  ctrl = (ctrl & ~0x1FU) | GPIO_FUNC_SIO;
  reg (SIO + SIO_OE_SET) = (1U << LED_PIN);
}

static SEC ("_text") void
led (bool on)
{
  reg (SIO + (on ? SIO_OUT_SET : SIO_OUT_CLR)) = (1U << LED_PIN);
}

// The app's entry (vector[1]): announce, then heartbeat until a key arrives on
// the UART; consume the key and RETURN so the supervisor switches apps. State
// (app_counter) persists in this app's PSRAM .bss across activations.
static SEC ("_text") void
app_entry (void)
{
  app_print (MSG_BANNER);
  led_init ();
  for (;;)
    {
      app_counter = app_counter + 1;
      led ((app_counter & 1) != 0);
      app_print (MSG_HB);
      app_print_dec (app_counter);
      app_print (MSG_NL);

      // Busy-wait, polling the UART. Switch ONLY on the ESC key (0x1B). Every
      // other byte — the echo of our own printed output, loopback traffic, RX
      // noise — is read and discarded, so the RX FIFO stays drained and only a
      // real ESC yields back to the supervisor. (0x1B never appears in the
      // app's own printable output, so echo can't trigger it.)
      for (volatile std::uint32_t d = 0; d < 6000000U; ++d)
        {
          if (rx_pending ())
            {
              std::uint32_t dr = reg (UART0 + UART_DR);
              if ((dr & 0xF00U) == 0)
                {
                  std::uint32_t c = dr & 0xFFU;
                  if (c == 0x1BU) // ESC -> supervisor switches apps
                    {
                      led (false);
                      return;
                    }
                  if (c == 0x78U) // 'x' -> supervisor ends with PASS
                    {
                      led (false);
                      g_psram_stop = 2U;
                      return;
                    }
                }
            }
        }
    }
}

static SEC ("_text") void
app_default_handler (void)
{
  for (;;)
    {
    }
}

// ---- The app's OWN vector table (first in the PSRAM image => VTOR target) ---
SEC ("_vectors")
void (*const app_vectors[]) (void) = {
  reinterpret_cast<void (*) (void)> (__StackTop), // [0] initial MSP
  app_entry,            // [1] reset/entry (the supervisor calls this)
  app_default_handler,  // [2] NMI
  app_default_handler,  // [3] HardFault
  app_default_handler,  // [4] MemManage
  app_default_handler,  // [5] BusFault
  app_default_handler,  // [6] UsageFault
  app_default_handler,  // [7] SecureFault
  app_default_handler,  // [8] reserved
  app_default_handler,  // [9] reserved
  app_default_handler,  // [10] reserved
  app_default_handler,  // [11] SVCall
  app_default_handler,  // [12] DebugMonitor
  app_default_handler,  // [13] reserved
  app_default_handler,  // [14] PendSV
  app_default_handler,  // [15] SysTick
};
