/*
 * uart.hpp — USART1 console driver for the WeAct F412RE (PA9/PA10).
 *
 * Reception is interrupt-driven (RXNE); transmission is DMA-driven
 * (DMA2 Stream7, channel 4). Both are safe to call from RTOS threads:
 * put_char()/write() block while the TX ring is full, read_char() blocks
 * until a byte is received.
 */

#ifndef WEACT_F412RE_UART_HPP_
#define WEACT_F412RE_UART_HPP_

#include <cstddef>
#include <cstdint>

namespace uart
{
  // Configure USART1 (8N1), the RXNE interrupt and the TX DMA.
  void
  init (std::uint32_t baud);

  // Enqueue one byte (blocks while the TX ring is full). The DMA drains it.
  void
  put_char (char c);

  // Enqueue a NUL-terminated string; '\n' is expanded to "\r\n".
  void
  write (const char* s);

  // Enqueue n bytes; '\n' is expanded to "\r\n".
  void
  write (const char* s, std::size_t n);

  // Block until a received byte is available, then return it (0..255).
  int
  read_char (void);

  // Wait up to timeout_ms for a byte; return -1 on timeout.
  int
  timed_read_char (std::uint32_t timeout_ms);

  // Non-blocking: return the next received byte, or -1 if none.
  int
  try_read_char (void);

  // Number of bytes waiting in the RX ring.
  std::size_t
  rx_available (void);
}

#endif /* WEACT_F412RE_UART_HPP_ */
