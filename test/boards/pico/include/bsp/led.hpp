/*
 * led.hpp — onboard LED (GPIO25) for the Raspberry Pi Pico.
 */
#ifndef PICO_BSP_LED_HPP_
#define PICO_BSP_LED_HPP_

namespace led
{
  void
  init (void);
  void
  on (void);
  void
  off (void);
}

#endif /* PICO_BSP_LED_HPP_ */
