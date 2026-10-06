/*
 * led.cpp — user LED (PC13 / GPIOC) driver for the WeAct F411CE.
 */

#include <led.hpp>
#include <board.hpp>

#include "stm32f4xx.h"

namespace led
{
  void
  init (void)
  {
    // Enable the GPIOC peripheral clock.
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void) RCC->AHB1ENR; // read-back to ensure the clock is up before use

    const std::uint32_t pin = board::LED_PIN;

    // MODER: 01 = general-purpose output.
    GPIOC->MODER &= ~(0x3U << (pin * 2U));
    GPIOC->MODER |= (0x1U << (pin * 2U));

    // OTYPER: 0 = push-pull. OSPEEDR: low speed is fine for an LED.
    GPIOC->OTYPER &= ~(0x1U << pin);

    // PUPDR: no pull.
    GPIOC->PUPDR &= ~(0x3U << (pin * 2U));

    off ();
  }

  void
  on (void)
  {
    GPIOC->BSRR = (1U << board::LED_PIN); // atomic set
  }

  void
  off (void)
  {
    GPIOC->BSRR = (1U << (board::LED_PIN + 16U)); // atomic reset
  }

  void
  toggle (void)
  {
    GPIOC->ODR ^= (1U << board::LED_PIN);
  }
}
