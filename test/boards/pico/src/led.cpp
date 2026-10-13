/*
 * led.cpp — onboard LED for the Raspberry Pi Pico (board-selectable GPIO, see
 * rp2040.hpp). The Pico 2 board's led.cpp, on the RP2040 register map.
 */

#include <bsp/led.hpp>
#include <bsp/rp2040.hpp>

using rp2040::reg;

namespace
{
  constexpr std::uint32_t GPIO_FUNC_SIO = 5;
  constexpr std::uint32_t PADS_IE = 1U << 6, PADS_SCHMITT = 1U << 5;

  // The RP2040 has 30 GPIOs: one 32-bit SIO register covers them all.
  inline void
  sio_gpio_bit (std::uint32_t sio_reg, std::uint32_t pin)
  {
    reg (sio_reg) = 1U << pin;
  }
}

namespace led
{
  void
  init (void)
  {
    const std::uint32_t pin = rp2040::LED_PIN;
    // CTRL for GPIO n at IO_BANK0 + n*8 + 4; PAD reg at PADS + 0x04 + n*4.
    volatile std::uint32_t& gpio_ctrl = reg (rp2040::IO_BANK0_BASE + pin * 8 + 4);
    volatile std::uint32_t& pad_ctrl = reg (rp2040::PADS_BANK0_BASE + 0x04 + pin * 4);

    pad_ctrl = pad_ctrl | PADS_IE | PADS_SCHMITT;
    gpio_ctrl = (gpio_ctrl & ~0x1FU) | GPIO_FUNC_SIO;

    off ();
    sio_gpio_bit (rp2040::SIO_GPIO_OE_SET, pin); // enable output driver
  }

  void
  on (void)
  {
    sio_gpio_bit (rp2040::SIO_GPIO_OUT_SET, rp2040::LED_PIN);
  }

  void
  off (void)
  {
    sio_gpio_bit (rp2040::SIO_GPIO_OUT_CLR, rp2040::LED_PIN);
  }
}
