/*
 * psram.hpp - RP2350 QSPI PSRAM (secondary QSPI, XIP window 1) support.
 *
 * The RP2350 QMI has a second chip-select (CS1) for a secondary QSPI device
 * (e.g. PSRAM). On the WeAct Studio RP2350B the PSRAM CS is GPIO0. After
 * psram::init(), the PSRAM is mapped read-write at 0x11000000.
 */

#ifndef BSP_PSRAM_HPP_
#define BSP_PSRAM_HPP_

namespace psram
{
  /**
   * Detect, reset and enable quad mode on the PSRAM, and configure the QMI
   * M1 (window 1) read/write formats. Runs from the .psram_ram section
   * (internal SRAM) because the QMI direct-mode sequence cannot execute from
   * flash XIP.
   *
   * @return true if a test write+read back at 0x11000000 succeeded.
   */
  bool init (void);

  /**
   * Read the PSRAM device ID (SPI 0x9F) via QMI direct mode into out[0..7]:
   * out[0]=MFID (APS6404 = 0x0D), out[1]=KGD (0x5D), out[2..7]=EID. Issues
   * RSTEN/RST first, so call it before init() on a fresh (SPI-mode) PSRAM.
   */
  void read_id (unsigned char out[8]);
} // namespace psram

#endif /* BSP_PSRAM_HPP_ */
