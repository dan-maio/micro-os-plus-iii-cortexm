/*
 * led.cpp — user LED (PB2 / GPIOB) driver for the WeAct F412RE core board.
 */

#include <led.hpp>
#include <board.hpp>

#include "stm32f4xx.h"

namespace led
{
  void
  init (void)
  {
    // Enable the GPIOB peripheral clock.
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void) RCC->AHB1ENR; // read-back to ensure the clock is up before use

    const std::uint32_t pin = board::LED_PIN;

    // MODER: 01 = general-purpose output.
    GPIOB->MODER &= ~(0x3U << (pin * 2U));
    GPIOB->MODER |= (0x1U << (pin * 2U));

    // OTYPER: 0 = push-pull. OSPEEDR: low speed is fine for an LED.
    GPIOB->OTYPER &= ~(0x1U << pin);

    // PUPDR: no pull.
    GPIOB->PUPDR &= ~(0x3U << (pin * 2U));

    off ();
  }

  void
  on (void)
  {
    GPIOB->BSRR = (1U << board::LED_PIN); // atomic set
  }

  void
  off (void)
  {
    GPIOB->BSRR = (1U << (board::LED_PIN + 16U)); // atomic reset
  }

  void
  toggle (void)
  {
    GPIOB->ODR ^= (1U << board::LED_PIN);
  }
}
