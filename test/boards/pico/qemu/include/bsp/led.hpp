/*
 * led.hpp — no-op LED for the pico QEMU variant.
 *
 * The generic Cortex-M3 machine (mps2-an385) has no GPIO25; the tests only toggle the LED
 * for feedback, so this is inert.
 */
#ifndef PICO_QEMU_BSP_LED_HPP_
#define PICO_QEMU_BSP_LED_HPP_

namespace led
{
  void
  init (void);
  void
  on (void);
  void
  off (void);
}

#endif /* PICO_QEMU_BSP_LED_HPP_ */
