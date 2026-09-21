/*
 * led.hpp — user LED (PB2 / GPIOB) driver for the WeAct F412RE.
 */

#ifndef WEACT_F412RE_LED_HPP_
#define WEACT_F412RE_LED_HPP_

namespace led
{
  // Enable GPIOB clock and configure PB2 as a push-pull output.
  void
  init (void);

  void
  on (void);

  void
  off (void);

  void
  toggle (void);
}

#endif /* WEACT_F412RE_LED_HPP_ */
