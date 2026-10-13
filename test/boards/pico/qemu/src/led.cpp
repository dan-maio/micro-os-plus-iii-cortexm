/*
 * led.cpp — the pico QEMU variant's LED, which is inert (the generic
 * Cortex-M3 machine (mps2-an385) has no GPIO25).
 */
#include <bsp/led.hpp>

namespace led
{
  void
  init (void)
  {
  }

  void
  on (void)
  {
  }

  void
  off (void)
  {
  }
}
