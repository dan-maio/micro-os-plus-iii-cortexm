/*
 * rp2040.hpp — RP2040 (Raspberry Pi Pico) register map for the µOS++ port.
 *
 * Bare-register definitions (no pico-sdk), from the RP2040 datasheet, laid
 * out as the Pico 2 board's rp2350.hpp so the two boards' code reads alike.
 */

#ifndef PICO_BSP_RP2040_HPP_
#define PICO_BSP_RP2040_HPP_

#include <cstdint>

namespace rp2040
{
  // ---- Peripheral bases ---------------------------------------------------
  inline constexpr std::uint32_t CLOCKS_BASE = 0x40008000U;
  inline constexpr std::uint32_t RESETS_BASE = 0x4000C000U;
  inline constexpr std::uint32_t PSM_BASE = 0x40010000U;
  inline constexpr std::uint32_t IO_BANK0_BASE = 0x40014000U;
  inline constexpr std::uint32_t PADS_BANK0_BASE = 0x4001C000U;
  inline constexpr std::uint32_t XOSC_BASE = 0x40024000U;   // 12 MHz crystal
  inline constexpr std::uint32_t PLL_SYS_BASE = 0x40028000U;
  inline constexpr std::uint32_t PLL_USB_BASE = 0x4002C000U;
  inline constexpr std::uint32_t UART0_BASE = 0x40034000U;
  inline constexpr std::uint32_t ADC_BASE = 0x4004C000U;
  inline constexpr std::uint32_t TIMER_BASE = 0x40054000U;
  inline constexpr std::uint32_t WATCHDOG_BASE = 0x40058000U;
  inline constexpr std::uint32_t VREG_AND_CHIP_RESET_BASE = 0x40064000U;

  // RESETS bits (datasheet 2.14.3).
  inline constexpr std::uint32_t RESET_IO_BANK0 = 1U << 5;
  inline constexpr std::uint32_t RESET_PADS_BANK0 = 1U << 8;
  inline constexpr std::uint32_t RESET_PLL_SYS = 1U << 12;
  inline constexpr std::uint32_t RESET_PLL_USB = 1U << 13;
  inline constexpr std::uint32_t RESET_TIMER = 1U << 21;
  inline constexpr std::uint32_t RESET_UART0 = 1U << 22;
  inline constexpr std::uint32_t RESET_USBCTRL = 1U << 24;

  // TIMER: TIMERAWH/TIMERAWL (raw, unlatched) and the alarm registers.
  inline constexpr std::uint32_t TIMER_ALARM0 = TIMER_BASE + 0x10U;
  inline constexpr std::uint32_t TIMER_TIMERAWH = TIMER_BASE + 0x24U;
  inline constexpr std::uint32_t TIMER_TIMERAWL = TIMER_BASE + 0x28U;
  inline constexpr std::uint32_t TIMER_INTR = TIMER_BASE + 0x34U;
  inline constexpr std::uint32_t TIMER_INTE = TIMER_BASE + 0x38U;

  // WATCHDOG_TICK: the 1 us tick the TIMER counts (CYCLES | ENABLE).
  inline constexpr std::uint32_t WATCHDOG_TICK = WATCHDOG_BASE + 0x2CU;

  // ---- SIO (single-cycle IO): CPUID, GPIO, FIFO, hardware spinlocks -------
  inline constexpr std::uint32_t SIO_BASE = 0xD0000000U;
  inline constexpr std::uint32_t SIO_CPUID = SIO_BASE + 0x000U;
  inline constexpr std::uint32_t SIO_GPIO_OUT_SET = SIO_BASE + 0x014U;
  inline constexpr std::uint32_t SIO_GPIO_OUT_CLR = SIO_BASE + 0x018U;
  inline constexpr std::uint32_t SIO_GPIO_OE_SET = SIO_BASE + 0x024U;
  inline constexpr std::uint32_t SIO_FIFO_ST = SIO_BASE + 0x050U;
  inline constexpr std::uint32_t SIO_FIFO_WR = SIO_BASE + 0x054U;
  inline constexpr std::uint32_t SIO_FIFO_RD = SIO_BASE + 0x058U;
  inline constexpr std::uint32_t SIO_SPINLOCK0 = SIO_BASE + 0x100U;  // 32 locks
  inline constexpr std::uint32_t SIO_SPINLOCK_COUNT = 32U;

  inline constexpr std::uint32_t FIFO_ST_VLD = 1U << 0;   // read data valid
  inline constexpr std::uint32_t FIFO_ST_RDY = 1U << 1;   // write space ready

  // ---- Cortex-M0+ core (PPB) ---------------------------------------------
  inline constexpr std::uint32_t PPB_VTOR = 0xE000ED08U;
  inline constexpr std::uint32_t SYST_CSR = 0xE000E010U;
  inline constexpr std::uint32_t SYST_RVR = 0xE000E014U;
  inline constexpr std::uint32_t SYST_CVR = 0xE000E018U;

  // ---- Board facts --------------------------------------------------------
  inline constexpr std::uint32_t SYS_CLK_HZ = 125000000U;
  // Onboard LED. Board-selectable at build time (-DPICO_LED_PIN=n); the
  // Raspberry Pi Pico's is GPIO25.
#if !defined(PICO_LED_PIN)
#define PICO_LED_PIN 25
#endif
  inline constexpr std::uint32_t LED_PIN = PICO_LED_PIN;    // onboard LED
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

  // The 64-bit microsecond TIMER (TIMERAWH/TIMERAWL have no read latch, so
  // read the high word on both sides of the low one).
  inline std::uint64_t
  time_us (void)
  {
    std::uint32_t hi = reg (TIMER_TIMERAWH);
    for (;;)
      {
        std::uint32_t lo = reg (TIMER_TIMERAWL);
        std::uint32_t hi2 = reg (TIMER_TIMERAWH);
        if (hi == hi2)
          {
            return (static_cast<std::uint64_t> (hi) << 32) | lo;
          }
        hi = hi2;
      }
  }
}

#endif /* PICO_BSP_RP2040_HPP_ */
