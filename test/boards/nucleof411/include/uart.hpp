/*
 * uart.hpp — USART2 console driver for the Nucleo-F411RE (→ ST-Link VCP).
 *
 * Polled, blocking transmit. The caller serialises access (the test wraps
 * each line in a mutex), so the driver itself keeps no shared state beyond
 * the peripheral registers.
 */

#ifndef NUCLEO_F411RE_UART_HPP_
#define NUCLEO_F411RE_UART_HPP_

#include <cstdint>
#include <cstddef>

namespace uart
{
  // Configure PA2/PA3 (AF7), enable USART2 and set the baud rate from the
  // current SystemCoreClock (APB1 = SYSCLK on HSI, prescaler 1).
  void
  init (std::uint32_t baud);

  void
  put_char (char c);

  void
  write (const char* s);

  // Write exactly n bytes (n-safe for formatted buffers).
  void
  write (const char* s, std::size_t n);
}

#endif /* NUCLEO_F411RE_UART_HPP_ */
