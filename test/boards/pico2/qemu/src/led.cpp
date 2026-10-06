/*
 * led.cpp — the pico2 QEMU variant's LED, which is inert (the generic
 * Cortex-M33 machine has no GPIO25).
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
