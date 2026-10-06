/*
 * board.hpp — ST Nucleo-F411RE board definitions (STM32F411RE, Cortex-M4F).
 *
 * Bare-register BSP built on the CMSIS device header (no ST HAL). Naming
 * follows the project C++ rules: UPPERCASE constants, snake_case symbols.
 */

#ifndef NUCLEO_F411RE_BOARD_HPP_
#define NUCLEO_F411RE_BOARD_HPP_

#include <cstdint>

namespace board
{
  // User LED LD2 (green) is on PA5.
  inline constexpr std::uint32_t LED_PIN = 5U;

  // USART2 is wired to the ST-Link Virtual COM Port: TX=PA2, RX=PA3 (AF7).
  inline constexpr std::uint32_t UART_TX_PIN = 2U;
  inline constexpr std::uint32_t UART_RX_PIN = 3U;
  inline constexpr std::uint32_t UART_AF7 = 7U;

  // Console baud rate (matches the Cortex-A7 SMP suite default).
  inline constexpr std::uint32_t UART_BAUD = 115200U;
}

#endif /* NUCLEO_F411RE_BOARD_HPP_ */
