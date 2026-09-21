/*
 * uart.cpp — UART0 (PL011) console for the RP2350. Adapted from the Eclipse
 * reference (reference/src/uart.c). Polled, blocking TX.
 */

#include <bsp/uart.hpp>
#include <bsp/rp2350.hpp>
#include <cmsis_device.h>

using rp2350::reg;

namespace
{
  constexpr std::uint32_t DR = 0x00, FR = 0x18, IBRD = 0x24, FBRD = 0x28,
                          LCRH = 0x2C, CR = 0x30, IMSC = 0x38, ICR = 0x44;
  constexpr std::uint32_t FR_TXFF = 1U << 5;
  constexpr std::uint32_t FR_RXFE = 1U << 4;   // RX FIFO empty

  constexpr std::uint32_t GPIO_FUNC_UART0 = 2;
  constexpr std::uint32_t PADS_ISO = 1U << 8, PADS_IE = 1U << 6;
  constexpr std::uint32_t RESET_UART0 = 1U << 26, RESET_IO_BANK0 = 1U << 6,
                          RESET_PADS_BANK0 = 1U << 9;

  // Dual-core console: serialise UART access with SIO hardware spinlock 1
  // (spinlock 0 is the RTOS kernel lock). Without it the two cores' polled
  // TX writes interleave character-by-character.
  //
  // Ordering rules:
  //  - lock: mask IRQs BEFORE spinning on the spinlock. If the holder can be
  //    preempted between the spinlock write and the mask, the other core (or
  //    this core's own ISR) spins forever on a lock that is being held by a
  //    thread that cannot run. Masking first makes the whole hold atomic.
  //  - unlock: release the spinlock BEFORE restoring PRIMASK, and restore the
  //    caller's ORIGINAL PRIMASK (a write that starts inside a critical
  //    section must not unmask interrupts).
  constexpr std::uint32_t UART_LOCK = rp2350::SIO_SPINLOCK0 + 0x04; // slot 1

  // Saved PRIMASK per core (a write that begins with IRQs masked restores
  // them masked). Only one writer per core exists at any time (the spinlock
  // serialises cross-core, the mask serialises same-core).
  static std::uint32_t uart_saved_primask[2];
  void
  lock_uart (void)
  {
    unsigned cpu = rp2350::cpu_id ();
    uart_saved_primask[cpu] = __get_PRIMASK ();
    __asm__ volatile ("cpsid i" ::: "memory");
    while (reg (UART_LOCK) == 0)
      {
      }
  }
  void
  unlock_uart (void)
  {
    unsigned cpu = rp2350::cpu_id ();
    reg (UART_LOCK) = 1;
    __set_PRIMASK (uart_saved_primask[cpu]);
  }

  void
  unreset_block (std::uint32_t mask)
  {
    reg (rp2350::RESETS_BASE + 0x00) &= ~mask;
    while ((reg (rp2350::RESETS_BASE + 0x08) & mask) != mask)
      {
      }
  }
}

namespace uart
{
  void
  init (void)
  {
    unreset_block (RESET_UART0 | RESET_IO_BANK0 | RESET_PADS_BANK0);

    // UART0 pins, selectable per build (WeAct RP2350B default is 12/13).
    // CTRL is at IO_BANK0 + pin*8 + 4; function 2 = UART0.
#if !defined(PICO2_UART_TX_PIN)
#define PICO2_UART_TX_PIN 0
#endif
#if !defined(PICO2_UART_RX_PIN)
#define PICO2_UART_RX_PIN 1
#endif
    const std::uint32_t tx = PICO2_UART_TX_PIN;
    const std::uint32_t rx = PICO2_UART_RX_PIN;
    reg (rp2350::IO_BANK0_BASE + tx * 8 + 4)
        = (reg (rp2350::IO_BANK0_BASE + tx * 8 + 4) & ~0x1FU) | GPIO_FUNC_UART0;
    reg (rp2350::IO_BANK0_BASE + rx * 8 + 4)
        = (reg (rp2350::IO_BANK0_BASE + rx * 8 + 4) & ~0x1FU) | GPIO_FUNC_UART0;

    // Un-isolate the pads (PADS reg for pin n is at 0x04 + n*4).
    reg (rp2350::PADS_BANK0_BASE + 0x04 + tx * 4)
        = (reg (rp2350::PADS_BANK0_BASE + 0x04 + tx * 4) & ~PADS_ISO) | PADS_IE;
    reg (rp2350::PADS_BANK0_BASE + 0x04 + rx * 4)
        = (reg (rp2350::PADS_BANK0_BASE + 0x04 + rx * 4) & ~PADS_ISO) | PADS_IE;

    const std::uint32_t u = rp2350::UART0_BASE;
    reg (u + CR) = 0;               // disable while configuring
    reg (u + ICR) = 0x7FF;          // clear pending
    // Compute the baud divisor from the actual system clock (clk_peri ==
    // clk_sys), so the console stays at 115200 regardless of PICO2_SYS_CLK_KHZ
    // (150 MHz default, or e.g. 48 MHz on RP2350B boards whose flash cannot
    // sustain high-speed XIP reads). PL011: divisor = clk/(16*baud);
    // IBRD = integer part, FBRD = rounded fractional part * 64.
    {
      const std::uint32_t baud = 115200;
      const std::uint32_t clk = SystemCoreClock;
      const std::uint32_t d16 = 16u * baud;
      // PL011 FBRD is a 6-bit field (0..63); the rounded fractional part can
      // come out as exactly 64 (e.g. clk = 94 or 188 MHz), which truncates to
      // 0 in the register and leaves a ~1-2% baud error. Carry it into IBRD
      // instead (IBRD+1, FBRD=0) — the correct rounding of a 64/64 fraction.
      std::uint32_t ibrd = clk / d16;
      std::uint32_t fbrd = ((clk % d16) * 64u + d16 / 2u) / d16;
      if (fbrd >= 64u)
        {
          ibrd += 1u;
          fbrd -= 64u;
        }
      reg (u + IBRD) = ibrd;
      reg (u + FBRD) = fbrd;
    }
    reg (u + LCRH) = (3U << 5) | (1U << 4); // 8N1, FIFO enable
    reg (u + IMSC) = 0;             // polled
    reg (u + CR) = (1U << 9) | (1U << 8) | (1U << 0); // RXE | TXE | UARTEN
  }

  void
  put_char (char c)
  {
    while (reg (rp2350::UART0_BASE + FR) & FR_TXFF)
      {
      }
    reg (rp2350::UART0_BASE + DR) = static_cast<std::uint32_t> (
        static_cast<unsigned char> (c));
  }

  // Block until the TX path is idle: FR.BUSY (bit 3) stays set while the FIFO
  // or the shift register still holds data. Needed before jumping into a tight
  // loop (or resetting) so the final line is not truncated mid-shift.
  void
  flush (void)
  {
    constexpr std::uint32_t FR_BUSY = 1U << 3;
    while (reg (rp2350::UART0_BASE + FR) & FR_BUSY)
      {
      }
  }

  void
  write (const char* s)
  {
    lock_uart ();
    for (; *s != '\0'; ++s)
      {
        if (*s == '\n')
          {
            put_char ('\r');
          }
        put_char (*s);
      }
    unlock_uart ();
  }

  void
  write (const char* s, std::size_t n)
  {
    lock_uart ();
    for (std::size_t i = 0; i < n; ++i)
      {
        if (s[i] == '\n')
          {
            put_char ('\r');
          }
        put_char (s[i]);
      }
    unlock_uart ();
  }

  void
  print_hex32 (unsigned long v)
  {
    char buf[9];
    buf[8] = '\0';
    for (int i = 7; i >= 0; --i)
      {
        unsigned d = (v >> (i * 4)) & 0xFU;
        buf[7 - i] = static_cast<char> (d < 10 ? '0' + d : 'a' + d - 10);
      }
    write (buf, 8);
  }

  bool
  rx_ready (void)
  {
    return (reg (rp2350::UART0_BASE + FR) & FR_RXFE) == 0;
  }

  int
  get_char (void)
  {
    if (!rx_ready ())
      return -1;
    return static_cast<int> (reg (rp2350::UART0_BASE + DR) & 0xFFU);
  }
}
