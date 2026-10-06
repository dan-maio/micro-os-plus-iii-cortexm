/*
 * adc.cpp — RP2350 12-bit SAR ADC driver (bare-register).
 *
 * Sequence follows the datasheet: un-reset the ADC, set CS.EN and wait for
 * CS.READY, then per conversion write AINSEL + START_ONCE and poll READY.
 * clk_adc must already be running (clocks::adc_clk_init()).
 */

#include <bsp/adc.hpp>
#include <bsp/rp2350.hpp>

using rp2350::reg;

namespace
{
  constexpr std::uint32_t ADC_CS = 0x00, ADC_RESULT = 0x04;
  constexpr std::uint32_t CS_EN = 1U << 0, CS_START_ONCE = 1U << 2,
                          CS_READY = 1U << 8;
  constexpr std::uint32_t CS_AINSEL_LSB = 12, CS_AINSEL_BITS = 0xF000u;

  constexpr std::uint32_t RESETS_RESET = 0x00, RESETS_DONE = 0x08;
  constexpr std::uint32_t RESET_ADC = 1U << 0;

  // PADS_BANK0 per-GPIO registers start at offset 0x04, stride 4.
  constexpr std::uint32_t PAD_IE = 1U << 6, PAD_OD = 1U << 7;
}

namespace adc
{
  void
  init (void)
  {
    reg (rp2350::RESETS_BASE + RESETS_RESET) &= ~RESET_ADC;
    while (!(reg (rp2350::RESETS_BASE + RESETS_DONE) & RESET_ADC))
      {
      }
    reg (rp2350::ADC_BASE + ADC_CS) = CS_EN;
    while (!(reg (rp2350::ADC_BASE + ADC_CS) & CS_READY))
      {
      }
  }

  void
  gpio_init (unsigned gpio)
  {
    // Isolate the digital pad so the analog input is clean: IE=0, OD=1.
    const std::uint32_t pad = rp2350::PADS_BANK0_BASE + 0x04u + gpio * 4u;
    reg (pad) = (reg (pad) & ~PAD_IE) | PAD_OD;
  }

  std::uint16_t
  read (unsigned channel)
  {
    const std::uint32_t b = rp2350::ADC_BASE;
    reg (b + ADC_CS) = (reg (b + ADC_CS) & ~CS_AINSEL_BITS)
                       | CS_EN
                       | ((channel << CS_AINSEL_LSB) & CS_AINSEL_BITS)
                       | CS_START_ONCE;
    while (!(reg (b + ADC_CS) & CS_READY))
      {
      }
    return static_cast<std::uint16_t> (reg (b + ADC_RESULT) & 0x0FFFu);
  }
}
