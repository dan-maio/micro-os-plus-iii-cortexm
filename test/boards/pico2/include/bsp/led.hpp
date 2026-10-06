/*
 * led.hpp — onboard LED (GPIO25) for the Raspberry Pi Pico 2.
 */
#ifndef PICO2_BSP_LED_HPP_
#define PICO2_BSP_LED_HPP_

namespace led
{
  void
  init (void);
  void
  on (void);
  void
  off (void);
}

#endif /* PICO2_BSP_LED_HPP_ */
