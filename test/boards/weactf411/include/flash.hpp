/*
 * flash.hpp — W25Q-series SPI NOR flash driver for the WeAct F411CE.
 *
 * Opcodes are the standard Winbond set. Addressing is 24-bit (covers the
 * 8 MiB W25Q64 and the 16 MiB W25Q128). Every command runs between CS
 * assert/deassert with a settle either side.
 */

#ifndef WEACT_F411CE_FLASH_HPP_
#define WEACT_F411CE_FLASH_HPP_

#include <cstdint>
#include <cstddef>

namespace flash
{
  struct JedecId
  {
    std::uint8_t manufacturer;
    std::uint8_t mem_type;
    std::uint8_t capacity;
  };

  JedecId
  read_id ();

  // Read len bytes from a 24-bit address.
  void
  read (std::uint32_t addr, std::uint8_t* out, std::size_t len);

  // Program into erased memory, splitting across 256-byte pages. Does not
  // erase; the region must be 0xFF already.
  bool
  write (std::uint32_t addr, const std::uint8_t* data, std::size_t len);

  // Program one page (<= 256 bytes) into erased memory.
  bool
  page_program (std::uint32_t addr, const std::uint8_t* data, std::size_t len);

  // Erase the 4 KiB sector containing addr.
  bool
  sector_erase (std::uint32_t addr);

  // Status register 1 (0x05): bit 0 is BUSY.
  std::uint8_t
  read_status1 ();
}

#endif /* WEACT_F411CE_FLASH_HPP_ */
