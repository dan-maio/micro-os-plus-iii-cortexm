/*
 * clocks.hpp — RP2040 clock bring-up (XOSC + PLL_SYS → 125 MHz).
 */
#ifndef PICO_BSP_CLOCKS_HPP_
#define PICO_BSP_CLOCKS_HPP_

#include <cstdint>

namespace clocks
{
  // Bring clk_sys = clk_peri = 125 MHz (PLL_SYS from the 12 MHz crystal,
  // VCO 1500 MHz / 6 / 2, the Pico SDK's default), clk_ref = XOSC, start the
  // 1 us watchdog tick the TIMER counts, and release IO_BANK0 / PADS_BANK0
  // from reset.
  void
  init (void);

  // Bring up the 48 MHz USB clock (PLL_USB + clk_usb) for the USB tests.
  // Returns true if PLL_USB locked and clk_usb is running (bounded wait).
  bool
  usb_init (void);
}

#endif /* PICO_BSP_CLOCKS_HPP_ */
