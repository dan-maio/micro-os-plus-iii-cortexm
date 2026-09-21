/*
 * led.hpp — user LED (PC13 / GPIOC) driver for the WeAct F411CE.
 */

#ifndef WEACT_F411CE_LED_HPP_
#define WEACT_F411CE_LED_HPP_

namespace led
{
  // Enable GPIOC clock and configure PC13 as a push-pull output.
  void
  init (void);

  void
  on (void);

  void
  off (void);

  void
  toggle (void);
}

#endif /* WEACT_F411CE_LED_HPP_ */
