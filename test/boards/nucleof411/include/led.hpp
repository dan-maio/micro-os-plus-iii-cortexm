/*
 * led.hpp — user LED (LD2 / PA5) driver for the Nucleo-F411RE.
 */

#ifndef NUCLEO_F411RE_LED_HPP_
#define NUCLEO_F411RE_LED_HPP_

namespace led
{
  // Enable GPIOA clock and configure PA5 as a push-pull output.
  void
  init (void);

  void
  on (void);

  void
  off (void);

  void
  toggle (void);
}

#endif /* NUCLEO_F411RE_LED_HPP_ */
