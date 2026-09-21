/*
 * adc.hpp — RP2350 12-bit SAR ADC (bare-register, no pico-sdk).
 *
 * clk_adc must be running first (clocks::adc_clk_init()). On the RP2350B
 * 48-GPIO package the ADC inputs are GPIO40..47 = channels 0..7.
 */
#ifndef PICO2_BSP_ADC_HPP_
#define PICO2_BSP_ADC_HPP_

#include <cstdint>

namespace adc
{
  // Un-reset + enable the ADC block. Call after clocks::adc_clk_init().
  void init (void);

  // Configure a GPIO as an analog input (disable its digital pad).
  void gpio_init (unsigned gpio);

  // One-shot 12-bit conversion on the given channel (0..7). Returns 0..4095.
  std::uint16_t read (unsigned channel);
}

#endif /* PICO2_BSP_ADC_HPP_ */
