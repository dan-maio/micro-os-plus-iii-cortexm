/*
 * uart.hpp — UART0 (PL011) console for the RP2040, GPIO0=TX/GPIO1=RX, 115200.
 */
#ifndef PICO_BSP_UART_HPP_
#define PICO_BSP_UART_HPP_

#include <cstddef>

namespace uart
{
  void
  init (void);          // 115200 8N1, clk_peri = 125 MHz
  void
  put_char (char c);
  void
  flush (void);          // block until the TX FIFO + shift reg are fully drained
  void
  write (const char* s);
  void
  write (const char* s, std::size_t n);
  void
  print_hex32 (unsigned long v);
  bool
  rx_ready (void);      // true if a byte is waiting in the RX FIFO
  int
  get_char (void);      // pop one RX byte (0..255), or -1 if none
}

#endif /* PICO_BSP_UART_HPP_ */
