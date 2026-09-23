/*
 * clocks.hpp — RP2350 clock bring-up (XOSC + PLL_SYS → 150 MHz).
 */
#ifndef PICO2_BSP_CLOCKS_HPP_
#define PICO2_BSP_CLOCKS_HPP_

#include <cstdint>

namespace clocks
{
  // Bring clk_sys = clk_peri = 150 MHz (PLL_SYS from the 12 MHz crystal),
  // and release IO_BANK0 / PADS_BANK0 from reset.
  //
  // When built with -DPICO2_CLOCK_FROM_SCRATCH, init() instead reads a target
  // frequency index from the watchdog scratch registers (SYS_TABLE below) and
  // programs the PLL + core voltage to that entry; on a cold boot (no valid
  // scratch) it defaults to 48 MHz. Other builds keep the compile-time
  // PICO2_SYS_CLK_KHZ path unchanged.
  void
  init (void);

  // Bring up the 48 MHz USB clock (PLL_USB + clk_usb) for the usb-hid driver.
  // Returns true if PLL_USB locked and clk_usb is running (bounded wait).
  bool
  usb_init (void);

  // ---- Runtime frequency table (smp-test-nested-clock) --------------------
  // One row per selectable system clock. clk_sys = 12 MHz * fbdiv / pd1 / pd2;
  // vsel is the POWMAN VREG code (0x0b = 1.10 V default). All rows keep the
  // VCO in the 750..1600 MHz PLL range and give <0.03% UART error at 115200.
  struct sys_entry
  {
    std::uint32_t khz;
    std::uint16_t fbdiv;
    std::uint8_t pd1;
    std::uint8_t pd2;
    std::uint8_t vsel;
  };
  // A build may add an experimental 250 MHz overclock row with
  // -DPICO2_ENABLE_250MHZ (smp-test-nested-clock only; NOT the _200 backup).
#ifdef PICO2_ENABLE_250MHZ
  inline constexpr unsigned SYS_TABLE_LEN = 13;
#else
  inline constexpr unsigned SYS_TABLE_LEN = 12;
#endif
  extern const sys_entry SYS_TABLE[SYS_TABLE_LEN];

  // The row init() selected this boot (index into SYS_TABLE), and its facts.
  unsigned      current_index (void);
  std::uint32_t current_khz (void);
  std::uint8_t  current_vsel (void);

  // POWMAN core-voltage select (see SYS_TABLE vsel). Raises/lowers Vcore; the
  // caller must set voltage BEFORE raising the clock (and lower it only after
  // dropping the clock). Blocks until the regulator settles.
  void vreg_set (std::uint8_t vsel);

  // Convert a vsel code to millivolts (for logging). vsel 0x0b = 1100 mV.
  std::uint32_t vsel_to_mv (std::uint8_t vsel);

  // Bring up clk_adc (PLL_USB 48 MHz) so the ADC block has a clock.
  void adc_clk_init (void);

  // Persist a target index in watchdog scratch and reboot into it. Never
  // returns.
  [[noreturn]] void reboot_to_index (unsigned idx);
}

#endif /* PICO2_BSP_CLOCKS_HPP_ */
