/*
 * led.hpp — no-op LED for the pico2 QEMU variant.
 *
 * The generic Cortex-M33 machine has no GPIO25; the tests only toggle the LED
 * for feedback, so this is inert.
 */
#ifndef PICO2_QEMU_BSP_LED_HPP_
#define PICO2_QEMU_BSP_LED_HPP_

namespace led
{
  void
  init (void);
  void
  on (void);
  void
  off (void);
}

#endif /* PICO2_QEMU_BSP_LED_HPP_ */
