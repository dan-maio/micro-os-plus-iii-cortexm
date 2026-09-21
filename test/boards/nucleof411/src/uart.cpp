/*
 * uart.cpp — USART2 console driver for the Nucleo-F411RE (→ ST-Link VCP).
 *
 * PA2 = USART2_TX, PA3 = USART2_RX, alternate function AF7. USART2 lives on
 * APB1, whose clock equals SystemCoreClock on the plain HSI configuration
 * (16 MHz, no prescaler), so the baud divisor is computed from that.
 */

#include <uart.hpp>
#include <board.hpp>

#include "stm32f4xx.h"

namespace
{
  void
  gpio_config_af (std::uint32_t pin, std::uint32_t af)
  {
    // MODER: 10 = alternate function.
    GPIOA->MODER &= ~(0x3U << (pin * 2U));
    GPIOA->MODER |= (0x2U << (pin * 2U));

    // OTYPER push-pull, high speed, no pull.
    GPIOA->OTYPER &= ~(0x1U << pin);
    GPIOA->OSPEEDR |= (0x3U << (pin * 2U));
    GPIOA->PUPDR &= ~(0x3U << (pin * 2U));

    // AFR[0] for pins 0..7, AFR[1] for pins 8..15.
    volatile std::uint32_t& afr = GPIOA->AFR[pin >> 3U];
    const std::uint32_t shift = (pin & 0x7U) * 4U;
    afr &= ~(0xFU << shift);
    afr |= (af << shift);
  }
}

namespace uart
{
  void
  init (std::uint32_t baud)
  {
    // Peripheral clocks: GPIOA (AHB1) and USART2 (APB1).
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void) RCC->APB1ENR;

    gpio_config_af (board::UART_TX_PIN, board::UART_AF7);
    gpio_config_af (board::UART_RX_PIN, board::UART_AF7);

    USART2->CR1 = 0U; // disable while configuring

    // BRR = round(f_apb1 / baud); with OVER8=0 the whole register is the
    // USARTDIV value in 12.4 fixed point, which equals f/baud.
    const std::uint32_t f_apb1 = SystemCoreClock;
    USART2->BRR = (f_apb1 + (baud / 2U)) / baud;

    // Enable transmitter, receiver and the USART itself (8N1, no parity).
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
  }

  void
  put_char (char c)
  {
    while ((USART2->SR & USART_SR_TXE) == 0U)
      {
        ; // wait for the transmit data register to be empty
      }
    USART2->DR = static_cast<std::uint32_t> (static_cast<unsigned char> (c));
  }

  void
  write (const char* s)
  {
    for (; *s != '\0'; ++s)
      {
        if (*s == '\n')
          {
            put_char ('\r');
          }
        put_char (*s);
      }
  }

  void
  write (const char* s, std::size_t n)
  {
    for (std::size_t i = 0; i < n; ++i)
      {
        if (s[i] == '\n')
          {
            put_char ('\r');
          }
        put_char (s[i]);
      }
  }
}
