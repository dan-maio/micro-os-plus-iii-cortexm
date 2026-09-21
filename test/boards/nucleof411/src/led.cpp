/*
 * led.cpp — user LED (LD2 / PA5) driver for the Nucleo-F411RE.
 */

#include <led.hpp>
#include <board.hpp>

#include "stm32f4xx.h"

namespace led
{
  void
  init (void)
  {
    // Enable the GPIOA peripheral clock.
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void) RCC->AHB1ENR; // read-back to ensure the clock is up before use

    const std::uint32_t pin = board::LED_PIN;

    // MODER: 01 = general-purpose output.
    GPIOA->MODER &= ~(0x3U << (pin * 2U));
    GPIOA->MODER |= (0x1U << (pin * 2U));

    // OTYPER: 0 = push-pull. OSPEEDR: low speed is fine for an LED.
    GPIOA->OTYPER &= ~(0x1U << pin);

    // PUPDR: no pull.
    GPIOA->PUPDR &= ~(0x3U << (pin * 2U));

    off ();
  }

  void
  on (void)
  {
    GPIOA->BSRR = (1U << board::LED_PIN); // atomic set
  }

  void
  off (void)
  {
    GPIOA->BSRR = (1U << (board::LED_PIN + 16U)); // atomic reset
  }

  void
  toggle (void)
  {
    GPIOA->ODR ^= (1U << board::LED_PIN);
  }
}
