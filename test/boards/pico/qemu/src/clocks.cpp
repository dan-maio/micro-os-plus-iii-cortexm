/*
 * clocks.cpp — the pico QEMU variant's clocks, which are inert: the generic
 * Cortex-M3 machine (mps2-an385)'s clock is whatever QEMU gives it. Only clocks::init() is
 * reached by the emulated tests; usb_init() is stubbed so the header links.
 */
// The board's own header: the QEMU variant stubs the same API.
#include "../../include/bsp/clocks.hpp"

namespace clocks
{
  void
  init (void)
  {
  }

  bool
  usb_init (void)
  {
    return false;
  }
}
