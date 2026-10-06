/*
 * uart.cpp — the pico2 QEMU variant's console, over the kernel's semihosting
 * trace (micro-os-plus::iii-trace-semihosting, which is bkpt 0xAB on
 * Cortex-M). Same API as the board's bsp/uart.cpp.
 */
#include <bsp/uart.hpp>

#include <cstddef>
#include <cmsis-plus/diag/trace.h>

namespace uart
{
  void
  init (void)
  {
  }

  void
  put_char (char c)
  {
    os::trace::putchar (static_cast<int> (static_cast<unsigned char> (c)));
  }

  void
  flush (void)
  {
  }

  void
  write (const char* s)
  {
    while (*s != '\0')
      {
        put_char (*s++);
      }
  }

  void
  write (const char* s, std::size_t n)
  {
    for (std::size_t i = 0; i < n; ++i)
      {
        put_char (s[i]);
      }
  }

  void
  print_hex32 (unsigned long v)
  {
    static const char digits[] = "0123456789abcdef";
    put_char ('0');
    put_char ('x');
    for (int shift = 28; shift >= 0; shift -= 4)
      {
        put_char (digits[(v >> shift) & 0xF]);
      }
  }

  bool
  rx_ready (void)
  {
    return false;
  }

  int
  get_char (void)
  {
    return -1;
  }
}
