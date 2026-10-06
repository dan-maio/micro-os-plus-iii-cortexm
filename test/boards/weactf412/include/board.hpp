/*
 * board.hpp — WeAct Studio STM32F412RE 64-pin core board definitions
 * (STM32F412RET6, Cortex-M4F).
 *
 * Bare-register BSP built on the CMSIS device header (no ST HAL). Naming
 * follows the project C++ rules: UPPERCASE constants, snake_case symbols.
 *
 * The WeAct STM32F412RE core board carries a 8 MHz HSE crystal, the user LED
 * on PB2 and the user key on PC13. The console is USART1 (PA9/PA10) and the
 * SPI1 header brings out CS PA4, SCK PA5, MISO PA6, MOSI PA7, exactly like the
 * sibling F411 board. The reference PLL configuration runs the core at
 * 100 MHz.
 */

#ifndef WEACT_F412RE_BOARD_HPP_
#define WEACT_F412RE_BOARD_HPP_

#include <cstdint>

namespace board
{
  // User LED is on PB2, active high (WeAct STM32F4 core board).
  inline constexpr std::uint32_t LED_PORT = 1U; // 0=GPIOA 1=GPIOB 2=GPIOC
  inline constexpr std::uint32_t LED_PIN = 2U;

  // USART1 console: TX=PA9, RX=PA10, alternate function AF7. USART1 is on
  // APB2, which at the 100 MHz PLL configuration runs at 100 MHz.
  inline constexpr std::uint32_t UART_TX_PIN = 9U;
  inline constexpr std::uint32_t UART_RX_PIN = 10U;
  inline constexpr std::uint32_t UART_AF7 = 7U;

  // Console baud rate.
  inline constexpr std::uint32_t UART_BAUD = 115200U;

  // SPI1 to the (optional) W25Q NOR flash: CS PA4, SCK PA5, MISO PA6, MOSI
  // PA7, alternate function AF5.
  inline constexpr std::uint32_t SPI_CS_PIN = 4U;
  inline constexpr std::uint32_t SPI_SCK_PIN = 5U;
  inline constexpr std::uint32_t SPI_MISO_PIN = 6U;
  inline constexpr std::uint32_t SPI_MOSI_PIN = 7U;
  inline constexpr std::uint32_t SPI_AF5 = 5U;

  // HSE crystal fitted on the WeAct STM32F412RE core board.
  inline constexpr std::uint32_t HSE_HZ = 8'000'000U;
  // The PLL configuration the reference firmware runs at.
  inline constexpr std::uint32_t SYSCLK_HZ = 100'000'000U;
}

#endif /* WEACT_F412RE_BOARD_HPP_ */
