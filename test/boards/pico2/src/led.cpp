/*
 * led.cpp — onboard LED for the Pico 2 / RP2350B (board-selectable GPIO, see
 * rp2350.hpp). Adapted from reference/src/gpio.c.
 */

#include <bsp/led.hpp>
#include <bsp/rp2350.hpp>

using rp2350::reg;

namespace
{
  constexpr std::uint32_t GPIO_FUNC_SIO = 5;
  constexpr std::uint32_t PADS_ISO = 1U << 8, PADS_IE = 1U << 6,
                          PADS_SCHMITT = 1U << 5;

  // SIO GPIO_OUT/OUT_SET/OUT_CLR/OE/OE_SET/OE_CLR are 64-bit registers
  // covering GPIO 0..47. Bits 32..47 live in the UPPER word (offset +4);
  // a plain 32-bit `1U << pin` silently becomes 0 for any pin >= 32 (e.g. the
  // Waveshare Core2350B LED on GPIO39), so write to the correct half.
  inline void
  sio_gpio_bit (std::uint32_t sio_reg, std::uint32_t pin)
  {
    const std::uint32_t off = sio_reg + ((pin >= 32u) ? 4u : 0u);
    reg (off) = 1U << (pin & 31u);
  }
}

namespace led
{
  void
  init (void)
  {
    const std::uint32_t pin = rp2350::LED_PIN;
    // CTRL for GPIO n at IO_BANK0 + n*8 + 4; PAD reg at PADS + 0x04 + n*4.
    volatile std::uint32_t& gpio_ctrl = reg (rp2350::IO_BANK0_BASE + pin * 8 + 4);
    volatile std::uint32_t& pad_ctrl = reg (rp2350::PADS_BANK0_BASE + 0x04 + pin * 4);

    pad_ctrl = (pad_ctrl & ~PADS_ISO) | PADS_IE | PADS_SCHMITT;
    gpio_ctrl = (gpio_ctrl & ~0x1FU) | GPIO_FUNC_SIO;

    off ();
    sio_gpio_bit (rp2350::SIO_GPIO_OE_SET, pin); // enable output driver
  }

  void
  on (void)
  {
    sio_gpio_bit (rp2350::SIO_GPIO_OUT_SET, rp2350::LED_PIN);
  }

  void
  off (void)
  {
    sio_gpio_bit (rp2350::SIO_GPIO_OUT_CLR, rp2350::LED_PIN);
  }
}
