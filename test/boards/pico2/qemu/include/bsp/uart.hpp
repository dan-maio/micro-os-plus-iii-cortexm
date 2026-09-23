/*
 * uart.hpp — semihosting console for the pico2 QEMU variant.
 *
 * Same API as the board's bsp/uart.hpp, so the tests are unchanged: only the
 * implementation behind it differs. On the generic Cortex-M33 machine there
 * is no PL011 at the board's address, so output goes through the kernel's
 * semihosting trace.
 */
#ifndef PICO2_QEMU_BSP_UART_HPP_
#define PICO2_QEMU_BSP_UART_HPP_

#include <cstddef>

namespace uart
{
  void
  init (void);
  void
  put_char (char c);
  void
  flush (void);
  void
  write (const char* s);
  void
  write (const char* s, std::size_t n);
  void
  print_hex32 (unsigned long v);
  bool
  rx_ready (void);
  int
  get_char (void);
}

#endif /* PICO2_QEMU_BSP_UART_HPP_ */
