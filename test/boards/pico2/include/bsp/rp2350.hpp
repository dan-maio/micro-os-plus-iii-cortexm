/*
 * rp2350.hpp — RP2350 (Raspberry Pi Pico 2) register map for the µOS++ port.
 *
 * Bare-register definitions (no pico-sdk). Naming follows the project rules:
 * UPPERCASE constants, snake_case symbols, kebab-case files.
 */

#ifndef PICO2_BSP_RP2350_HPP_
#define PICO2_BSP_RP2350_HPP_

#include <cstdint>

namespace rp2350
{
  // ---- Peripheral bases ---------------------------------------------------
  inline constexpr std::uint32_t XOSC_BASE = 0x40048000U;   // 12 MHz crystal
  inline constexpr std::uint32_t PLL_SYS_BASE = 0x40050000U;
  inline constexpr std::uint32_t CLOCKS_BASE = 0x40010000U;
  inline constexpr std::uint32_t RESETS_BASE = 0x40020000U;
  inline constexpr std::uint32_t IO_BANK0_BASE = 0x40028000U;
  inline constexpr std::uint32_t PADS_BANK0_BASE = 0x40038000U;
  inline constexpr std::uint32_t UART0_BASE = 0x40070000U;
  inline constexpr std::uint32_t PSM_BASE = 0x40018000U;
  inline constexpr std::uint32_t PLL_USB_BASE = 0x40058000U;
  inline constexpr std::uint32_t ADC_BASE = 0x400A0000U;    // 12-bit SAR ADC
  inline constexpr std::uint32_t POWMAN_BASE = 0x40100000U; // core VREG lives here
  inline constexpr std::uint32_t WATCHDOG_BASE = 0x400D8000U;
  inline constexpr std::uint32_t TICKS_BASE = 0x40108000U;

  // Potentiometer ADC input (RP2350B, 48-GPIO package): channels 0..7 map to
  // GPIO40..47. GPIO40 = ADC0. Wiper -> GPIO40, ends -> 3V3 and GND.
  inline constexpr std::uint32_t ADC_POT_PIN = 40U;
  inline constexpr std::uint32_t ADC_POT_CHANNEL = 0U;

  // ---- SIO (single-cycle IO): CPUID, GPIO, FIFO, hardware spinlocks -------
  inline constexpr std::uint32_t SIO_BASE = 0xD0000000U;
  inline constexpr std::uint32_t SIO_CPUID = SIO_BASE + 0x000U;
  inline constexpr std::uint32_t SIO_GPIO_OUT_SET = SIO_BASE + 0x018U;
  inline constexpr std::uint32_t SIO_GPIO_OUT_CLR = SIO_BASE + 0x020U;
  inline constexpr std::uint32_t SIO_GPIO_OE_SET = SIO_BASE + 0x038U;
  inline constexpr std::uint32_t SIO_FIFO_ST = SIO_BASE + 0x050U;
  inline constexpr std::uint32_t SIO_FIFO_WR = SIO_BASE + 0x054U;
  inline constexpr std::uint32_t SIO_FIFO_RD = SIO_BASE + 0x058U;
  inline constexpr std::uint32_t SIO_SPINLOCK0 = SIO_BASE + 0x100U;  // 32 locks
  inline constexpr std::uint32_t SIO_SPINLOCK_COUNT = 32U;

  inline constexpr std::uint32_t FIFO_ST_VLD = 1U << 0;   // read data valid
  inline constexpr std::uint32_t FIFO_ST_RDY = 1U << 1;   // write space ready

  // ---- Cortex-M33 core (PPB) ---------------------------------------------
  inline constexpr std::uint32_t PPB_VTOR = 0xE000ED08U;
  inline constexpr std::uint32_t PPB_CPACR = 0xE000ED88U;   // FPU access
  inline constexpr std::uint32_t SYST_CSR = 0xE000E010U;
  inline constexpr std::uint32_t SYST_RVR = 0xE000E014U;
  inline constexpr std::uint32_t SYST_CVR = 0xE000E018U;

  // ---- Board facts --------------------------------------------------------
  inline constexpr std::uint32_t SYS_CLK_HZ = 150000000U;
  // Onboard LED. Board-selectable at build time (-DPICO2_LED_PIN=n); the
  // WeAct RP2350B default is GPIO25.
#if !defined(PICO2_LED_PIN)
#define PICO2_LED_PIN 25
#endif
  inline constexpr std::uint32_t LED_PIN = PICO2_LED_PIN;   // onboard LED
  inline constexpr std::uint32_t UART_TX_PIN = 0U;          // GPIO0, func 2
  inline constexpr std::uint32_t UART_RX_PIN = 1U;          // GPIO1, func 2

  // ---- Register access helper --------------------------------------------
  inline volatile std::uint32_t&
  reg (std::uint32_t addr)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (addr);
  }

  // Current core index (0 or 1), read from SIO.
  inline unsigned
  cpu_id (void)
  {
    return static_cast<unsigned> (reg (SIO_CPUID));
  }
}

#endif /* PICO2_BSP_RP2350_HPP_ */
