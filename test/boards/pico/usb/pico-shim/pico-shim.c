/*
 * pico-shim.c — implements the ~10 pico-sdk platform functions the vendored
 * TinyUSB RP2040 device driver calls, mapped onto the RP2040: RESETS block,
 * NVIC (USBCTRL_IRQ 5), TIMER (microseconds), and a per-core PRIMASK
 * critical section. The Pico 2's pico-shim.c with the RP2040 addresses; see
 * test/boards/pico2/usb/README-tinyusb.md.
 */

#include <stdint.h>
#include <stdbool.h>

#include <cmsis_device.h>

#include "pico.h"
#include "hardware/resets.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/address_mapped.h"
#include "pico/critical_section.h"

// ---- register helpers -------------------------------------------------------
#define REG(a) (*(volatile uint32_t*) (a))
#define RESETS_RESET (RESETS_BASE + 0x0u)
#define RESETS_RESET_DONE (RESETS_BASE + 0x8u)

// RP2040 TIMER, counting the 1 us watchdog tick, as used by smp-test4.
#define TIMER0_BASE 0x40054000u
#define TIMER0_TIMERAWL (TIMER0_BASE + 0x28u)
#define TIMER0_RESET_BIT (1u << 21)
#define WATCHDOG_TICK 0x4005802cu
#define CLK_REF_HZ 12000000u

// ---- RESETS -----------------------------------------------------------------
void
reset_block (uint32_t bits)
{
  hw_set_bits ((io_rw_32*) RESETS_RESET, bits);
}

void
unreset_block_wait (uint32_t bits)
{
  hw_clear_bits ((io_rw_32*) RESETS_RESET, bits);
  while ((REG (RESETS_RESET_DONE) & bits) != bits)
    {
    }
}

// ---- NVIC (USBCTRL_IRQ) -----------------------------------------------------
// The dcd registers its handler here; the shared USBCTRL_IRQ_Handler (below,
// installed in the vector table via .after_vectors) dispatches to it.
static irq_handler_t g_usbctrl_handler = 0;

void
irq_set_enabled (uint32_t num, bool enabled)
{
  if (enabled)
    NVIC_EnableIRQ ((IRQn_Type) num);
  else
    NVIC_DisableIRQ ((IRQn_Type) num);
}

void
irq_add_shared_handler (uint32_t num, irq_handler_t handler,
                        uint8_t order_priority)
{
  (void) num;
  (void) order_priority;
  g_usbctrl_handler = handler; // single handler is sufficient here
}

void __attribute__ ((section (".after_vectors")))
USBCTRL_IRQ_Handler (void)
{
  if (g_usbctrl_handler)
    g_usbctrl_handler ();
}

// ---- TIMER0 (microsecond counter) ------------------------------------------
static void
timer0_init_once (void)
{
  static int done = 0;
  if (done)
    return;
  done = 1;
  hw_clear_bits ((io_rw_32*) RESETS_RESET, TIMER0_RESET_BIT);
  while (!(REG (RESETS_RESET_DONE) & TIMER0_RESET_BIT))
    {
    }
  REG (WATCHDOG_TICK) = (1u << 9) | (CLK_REF_HZ / 1000000u); // 1 MHz tick
}

uint32_t
time_us_32 (void)
{
  timer0_init_once ();
  return REG (TIMER0_TIMERAWL);
}

void
busy_wait_at_least_cycles (uint32_t cycles)
{
  // ~3 cycles per loop iteration; approximate is fine for the callers.
  volatile uint32_t n = cycles / 3u + 1u;
  while (n--)
    {
      __asm volatile ("nop");
    }
}

// ---- critical section (per-core PRIMASK) -----------------------------------
void
critical_section_init (critical_section_t* c)
{
  c->saved = 0;
}
void
critical_section_deinit (critical_section_t* c)
{
  (void) c;
}
void
critical_section_enter_blocking (critical_section_t* c)
{
  c->saved = __get_PRIMASK ();
  __disable_irq ();
}
void
critical_section_exit (critical_section_t* c)
{
  __set_PRIMASK (c->saved);
}

// ---- panic ------------------------------------------------------------------
void
mu_usb_panic (const char* msg)
{
  (void) msg;
  __disable_irq ();
  for (;;)
    {
    }
}

// SYSINFO_CHIP_ID.REVISION: 1 = B0/B1, 2 = B2 (as the SDK's version).
#define SYSINFO_CHIP_ID 0x40000000u

uint8_t
rp2040_chip_version (void)
{
  return (uint8_t) (REG (SYSINFO_CHIP_ID) >> 28);
}

void irq_remove_handler (uint32_t num, irq_handler_t handler)
{ (void) num; (void) handler; g_usbctrl_handler = 0; }
