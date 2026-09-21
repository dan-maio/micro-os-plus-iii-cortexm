/*
 * board.hpp — WeAct Studio F411CE "Black Pill" board definitions
 * (STM32F411CEU6, Cortex-M4F).
 *
 * Bare-register BSP built on the CMSIS device header (no ST HAL). Naming
 * follows the project C++ rules: UPPERCASE constants, snake_case symbols.
 *
 * The F411CE is pin-compatible-in-spirit with the reference "Black Pill" from
 * WeAct: user LED on PC13, console on USART1 (PA9/PA10), SPI NOR flash on SPI1
 * (CS PA4, SCK PA5, MISO PA6, MOSI PA7). The HSE is a 25 MHz crystal and the
 * board runs the reference 100 MHz PLL configuration.
 */

#ifndef WEACT_F411CE_BOARD_HPP_
#define WEACT_F411CE_BOARD_HPP_

#include <cstdint>

namespace board
{
  // User LED (blue/red) is on PC13, active high.
  inline constexpr std::uint32_t LED_PORT = 2U; // 0=GPIOA 1=GPIOB 2=GPIOC
  inline constexpr std::uint32_t LED_PIN = 13U;

  // USART1 console: TX=PA9, RX=PA10, alternate function AF7. USART1 is on
  // APB2, which at the 100 MHz PLL configuration runs at 100 MHz.
  inline constexpr std::uint32_t UART_TX_PIN = 9U;
  inline constexpr std::uint32_t UART_RX_PIN = 10U;
  inline constexpr std::uint32_t UART_AF7 = 7U;

  // Console baud rate.
  inline constexpr std::uint32_t UART_BAUD = 115200U;

  // SPI1 to the on-board W25Q NOR flash: CS PA4, SCK PA5, MISO PA6, MOSI PA7,
  // alternate function AF5.
  inline constexpr std::uint32_t SPI_CS_PIN = 4U;
  inline constexpr std::uint32_t SPI_SCK_PIN = 5U;
  inline constexpr std::uint32_t SPI_MISO_PIN = 6U;
  inline constexpr std::uint32_t SPI_MOSI_PIN = 7U;
  inline constexpr std::uint32_t SPI_AF5 = 5U;

  // HSE crystal fitted on the WeAct F411CE "Black Pill".
  inline constexpr std::uint32_t HSE_HZ = 25'000'000U;
  // The PLL configuration the reference firmware runs at.
  inline constexpr std::uint32_t SYSCLK_HZ = 100'000'000U;
}

#endif /* WEACT_F411CE_BOARD_HPP_ */
