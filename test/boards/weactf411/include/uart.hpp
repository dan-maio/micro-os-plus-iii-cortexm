/*
 * uart.hpp — USART1 console driver for the WeAct F411CE (PA9/PA10).
 *
 * Polled, blocking transmit. The caller serialises access, so the driver
 * itself keeps no shared state beyond the peripheral registers.
 */

#ifndef WEACT_F411CE_UART_HPP_
#define WEACT_F411CE_UART_HPP_

#include <cstdint>
#include <cstddef>

namespace uart
{
  // Configure PA9/PA10 (AF7), enable USART1 and set the baud rate from the
  // current SystemCoreClock. USART1 is on APB2, which in the BSP clock tree
  // (AHB /1, APB2 /1) runs at SystemCoreClock == 100 MHz, so the divisor is
  // computed from that value.
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

#endif /* WEACT_F411CE_UART_HPP_ */
