/*
 * clocks.cpp — the pico2 QEMU variant's clocks, which are inert: the generic
 * Cortex-M7 machine's clock is whatever QEMU gives it. Only clocks::init() is
 * reached by the emulated tests (exc-test, smp-test1, sc-test-ko); the rest
 * are stubbed so the header links.
 */
#include <bsp/clocks.hpp>

namespace clocks
{
  void
  init (void)
  {
  }

  bool
  usb_init (void)
  {
    return false;
  }

  const sys_entry SYS_TABLE[SYS_TABLE_LEN] = {};

  unsigned
  current_index (void)
  {
    return 0;
  }

  std::uint32_t
  current_khz (void)
  {
    return 0;
  }

  std::uint8_t
  current_vsel (void)
  {
    return 0;
  }

  void
  vreg_set (std::uint8_t)
  {
  }

  std::uint32_t
  vsel_to_mv (std::uint8_t)
  {
    return 0;
  }

  void
  adc_clk_init (void)
  {
  }

  [[noreturn]] void
  reboot_to_index (unsigned)
  {
    for (;;)
      {
      }
  }
}
