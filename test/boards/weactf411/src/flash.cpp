/*
 * flash.cpp — W25Q-series SPI NOR flash driver for the WeAct F411CE.
 *
 * Ported from the Rust `flash` module (which itself came from the WeAct
 * reference `w25q64.cpp`). Bounded busy-wait: BUSY is bit 0, and a bus with
 * nothing driving it reads 0xFF — which has bit 0 set, so an unbounded wait
 * on an absent part is "busy forever".
 */

#include <flash.hpp>

#include <spi.hpp>

namespace
{
  constexpr std::uint32_t WAIT_POLLS = 8000U;
  constexpr std::uint32_t WAIT_SPIN = 4000U;
  constexpr std::uint32_t CS_SETTLE = 24U;

  void
  spin_loop ()
  {
    __asm__ volatile ("" : : : "memory");
  }

  // The datasheet calls it tSHSL: CS# must stay high for 50 ns between a read
  // and the next command, 100 ns after a program/erase.
  void
  __attribute__ ((noinline))
  cs_settle ()
  {
    for (std::uint32_t i = 0; i < CS_SETTLE; ++i)
      {
        spin_loop ();
      }
  }

  std::uint8_t
  hi (std::uint32_t a)
  {
    return static_cast<std::uint8_t> ((a >> 16) & 0xFF);
  }
  std::uint8_t
  mid (std::uint32_t a)
  {
    return static_cast<std::uint8_t> ((a >> 8) & 0xFF);
  }
  std::uint8_t
  lo (std::uint32_t a)
  {
    return static_cast<std::uint8_t> (a & 0xFF);
  }

  void
  write_enable ()
  {
    cs_settle ();
    spi::cs_assert ();
    const std::uint8_t cmd = 0x06;
    spi::send (&cmd, 1);
    spi::cs_deassert ();
    cs_settle ();
  }

  // Block until the part is no longer busy.
  bool
  wait_idle ()
  {
    for (std::uint32_t i = 0; i < WAIT_POLLS; ++i)
      {
        if ((flash::read_status1 () & 0x01U) == 0U)
          {
            return true;
          }
        for (std::uint32_t s = 0; s < WAIT_SPIN; ++s)
          {
            spin_loop ();
          }
      }
    return false;
  }
}

namespace flash
{
  JedecId
  read_id ()
  {
    cs_settle ();
    spi::cs_assert ();
    const std::uint8_t cmd = 0x9F;
    spi::send (&cmd, 1);
    std::uint8_t out[3];
    spi::receive (out, 3);
    spi::cs_deassert ();
    cs_settle ();
    return JedecId{ out[0], out[1], out[2] };
  }

  void
  read (std::uint32_t addr, std::uint8_t* out, std::size_t len)
  {
    if (len == 0)
      {
        return;
      }
    cs_settle ();
    spi::cs_assert ();
    const std::uint8_t cmd[4] = { 0x03, hi (addr), mid (addr), lo (addr) };
    spi::send (cmd, 4);
    spi::receive (out, len);
    spi::cs_deassert ();
    cs_settle ();
  }

  bool
  page_program (std::uint32_t addr, const std::uint8_t* data, std::size_t len)
  {
    if (len == 0)
      {
        return true;
      }
    write_enable ();
    cs_settle ();
    spi::cs_assert ();
    const std::uint8_t cmd[4] = { 0x02, hi (addr), mid (addr), lo (addr) };
    spi::send (cmd, 4);
    spi::send (data, len);
    spi::cs_deassert ();
    cs_settle ();
    return wait_idle ();
  }

  bool
  write (std::uint32_t addr, const std::uint8_t* data, std::size_t len)
  {
    std::uint32_t a = addr;
    std::size_t pos = 0;
    while (pos < len)
      {
        const std::size_t page_room = 256U - (a & 0xFFU);
        const std::size_t chunk = page_room < (len - pos) ? page_room
                                                          : (len - pos);
        if (!page_program (a, data + pos, chunk))
          {
            return false;
          }
        pos += chunk;
        a += static_cast<std::uint32_t> (chunk);
      }
    return true;
  }

  bool
  sector_erase (std::uint32_t addr)
  {
    write_enable ();
    cs_settle ();
    spi::cs_assert ();
    const std::uint8_t cmd[4] = { 0x20, hi (addr), mid (addr), lo (addr) };
    spi::send (cmd, 4);
    spi::cs_deassert ();
    cs_settle ();
    return wait_idle ();
  }

  std::uint8_t
  read_status1 ()
  {
    cs_settle ();
    spi::cs_assert ();
    const std::uint8_t cmd = 0x05;
    spi::send (&cmd, 1);
    std::uint8_t out;
    spi::receive (&out, 1);
    spi::cs_deassert ();
    cs_settle ();
    return out;
  }
}
