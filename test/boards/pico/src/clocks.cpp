/*
 * clocks.cpp — RP2040 clock configuration (XOSC + PLL_SYS → 125 MHz).
 *
 * The Pico 2 board's sequence (test/boards/pico2/src/clocks.cpp) with the
 * RP2040's register map, and the frequency the Pico SDK runs the RP2040 at:
 * start XOSC (12 MHz) → move clk_sys/clk_ref off any AUX mux → PLL_SYS
 * VCO = 12*125 = 1500 MHz, /6 /2 = 125 MHz → clk_ref = XOSC,
 * clk_sys = PLL_SYS, clk_peri = clk_sys. No Pico SDK.
 */

#include <bsp/clocks.hpp>
#include <bsp/rp2040.hpp>

using rp2040::reg;

namespace
{
  // Register offsets.
  constexpr std::uint32_t XOSC_CTRL = 0x00, XOSC_STATUS = 0x04,
                          XOSC_STARTUP = 0x0C;
  constexpr std::uint32_t PLL_CS = 0x00, PLL_PWR = 0x04, PLL_FBDIV_INT = 0x08,
                          PLL_PRIM = 0x0C;
  constexpr std::uint32_t CLK_REF_CTRL = 0x30, CLK_SYS_CTRL = 0x3C,
                          CLK_PERI_CTRL = 0x48, CLK_USB_CTRL = 0x54;
  constexpr std::uint32_t CLK_DIV = 0x04, CLK_SELECTED = 0x08;
  constexpr std::uint32_t CLK_SYS_RESUS_CTRL = 0x78;
  constexpr std::uint32_t RESETS_RESET = 0x00, RESETS_DONE = 0x08;

  // Bit fields.
  constexpr std::uint32_t XOSC_FREQ_1_15MHZ = 0x0AA0, XOSC_ENABLE = 0x0FAB;
  constexpr std::uint32_t XOSC_STABLE = 1U << 31;
  constexpr std::uint32_t PLL_PWR_PD = 1U << 0, PLL_PWR_POSTDIVPD = 1U << 3,
                          PLL_PWR_VCOPD = 1U << 5, PLL_CS_LOCK = 1U << 31;
  constexpr std::uint32_t CLK_REF_SRC = 0x3, CLK_REF_SRC_XOSC = 2;
  constexpr std::uint32_t CLK_SYS_SRC = 0x1, CLK_SYS_SRC_AUX = 1;
  constexpr std::uint32_t CLK_SYS_AUX = 0xE0, CLK_SYS_AUX_LSB = 5;
  constexpr std::uint32_t CLK_REF_AUX = 0x60, CLK_REF_AUX_LSB = 5;
  constexpr std::uint32_t CLK_AUX = 0xE0, CLK_AUX_LSB = 5,
                          CLK_ENABLE = 1U << 11;
  // RP2040 clock dividers: integer part in bits 31:8.
  constexpr std::uint32_t CLK_DIV_UNDIVIDED = 1U << 8;

  void
  reset_block (std::uint32_t mask)
  {
    reg (rp2040::RESETS_BASE + RESETS_RESET) |= mask;
    // Wait a couple of cycles for the reset to propagate
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");
  }

  void
  unreset_block (std::uint32_t mask)
  {
    reg (rp2040::RESETS_BASE + RESETS_RESET) &= ~mask;
    while ((reg (rp2040::RESETS_BASE + RESETS_DONE) & mask) != mask)
      {
      }
  }

  void
  xosc_init (void)
  {
    // ~1 ms start-up delay at 12 MHz, in units of 256 cycles (as the SDK).
    reg (rp2040::XOSC_BASE + XOSC_STARTUP) = 47;
    reg (rp2040::XOSC_BASE + XOSC_CTRL)
        = (XOSC_ENABLE << 12) | XOSC_FREQ_1_15MHZ;
    while (!(reg (rp2040::XOSC_BASE + XOSC_STATUS) & XOSC_STABLE))
      {
      }
  }

  // Program a PLL to VCO = 12 MHz * fbdiv, then /postdiv1 /postdiv2. The PLL
  // is reset first: writing FBDIV/PRIM to a PLL that a previous run left
  // locked is ignored by the hardware (a debugger reset does not reset it).
  void
  pll_program (std::uint32_t base, std::uint32_t reset_bit,
               std::uint32_t fbdiv, std::uint32_t postdiv1,
               std::uint32_t postdiv2)
  {
    reset_block (reset_bit);
    unreset_block (reset_bit);
    reg (base + PLL_CS) = 1; // REFDIV = 1
    reg (base + PLL_FBDIV_INT) = fbdiv;
    reg (base + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
    while (!(reg (base + PLL_CS) & PLL_CS_LOCK))
      {
      }
    reg (base + PLL_PRIM) = (postdiv1 << 16) | (postdiv2 << 12);
    reg (base + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;
  }

  // Configure a glitchless clock (clk_ref / clk_sys).
  void
  clk_configure (std::uint32_t ctrl_addr, std::uint32_t src_mask,
                 std::uint32_t aux_mask, std::uint32_t aux_lsb,
                 std::uint32_t src, std::uint32_t auxsrc)
  {
    reg (ctrl_addr) = (reg (ctrl_addr) & ~aux_mask) | (auxsrc << aux_lsb);
    reg (ctrl_addr) = (reg (ctrl_addr) & ~src_mask) | (src << 0);
    while (!(reg (ctrl_addr + CLK_SELECTED) & (1U << src)))
      {
      }
    reg (ctrl_addr + CLK_DIV) = CLK_DIV_UNDIVIDED;
  }
}

// SystemCoreClock lives in soc/rp2040/src/system_rp2040.c.
extern "C" std::uint32_t SystemCoreClock;

namespace clocks
{
  void
  init (void)
  {
    const std::uint32_t c = rp2040::CLOCKS_BASE;

    // No clk_sys resus (a stopped clk_sys would re-route it behind our back).
    reg (c + CLK_SYS_RESUS_CTRL) = 0;

    xosc_init ();

    // Move clk_sys/clk_ref off any AUX source before touching the PLLs.
    reg (c + CLK_SYS_CTRL) &= ~CLK_SYS_SRC;
    while (!(reg (c + CLK_SYS_CTRL + CLK_SELECTED) & 1U))
      {
      }
    reg (c + CLK_REF_CTRL) &= ~CLK_REF_SRC;
    while (!(reg (c + CLK_REF_CTRL + CLK_SELECTED) & 1U))
      {
      }

    // PLL_SYS: 12 MHz * 125 = 1500 MHz VCO, /6 /2 = 125 MHz.
    pll_program (rp2040::PLL_SYS_BASE, rp2040::RESET_PLL_SYS, 125, 6, 2);

    // clk_ref = XOSC (12 MHz)
    clk_configure (c + CLK_REF_CTRL, CLK_REF_SRC, CLK_REF_AUX, CLK_REF_AUX_LSB,
                   CLK_REF_SRC_XOSC, 0);
    // clk_sys = PLL_SYS
    clk_configure (c + CLK_SYS_CTRL, CLK_SYS_SRC, CLK_SYS_AUX, CLK_SYS_AUX_LSB,
                   CLK_SYS_SRC_AUX, 0 /* AUXSRC_PLL_SYS */);

    // clk_peri = clk_sys — the UART reference clock.
    reg (c + CLK_PERI_CTRL) &= ~CLK_ENABLE;
    reg (c + CLK_PERI_CTRL)
        = (reg (c + CLK_PERI_CTRL) & ~CLK_AUX) | (0U << CLK_AUX_LSB);
    reg (c + CLK_PERI_CTRL) |= CLK_ENABLE;

    unreset_block (rp2040::RESET_IO_BANK0 | rp2040::RESET_PADS_BANK0
                   | rp2040::RESET_TIMER);

    // The watchdog tick the TIMER counts: 12 clk_ref (12 MHz) cycles = 1 us.
    reg (rp2040::WATCHDOG_TICK) = (1U << 9) | 12U;

    SystemCoreClock = rp2040::SYS_CLK_HZ;
  }

  // Bring up the 48 MHz USB clock (PLL_USB + clk_usb). Required by the USB
  // controller; not part of the default clock tree. Call once before
  // tusb_init(). Returns true if PLL_USB locked and clk_usb is running.
  bool
  usb_init (void)
  {
    const std::uint32_t b = rp2040::PLL_USB_BASE;
    const std::uint32_t c = rp2040::CLOCKS_BASE;

    // Hold the USB controller in reset while PLL_USB / clk_usb change; a
    // glitched clk_usb desyncs its clock domain. tusb_init() releases it.
    reset_block (rp2040::RESET_USBCTRL);

    // Take clk_usb down before reprogramming PLL_USB underneath it.
    reg (c + CLK_USB_CTRL) &= ~CLK_ENABLE;

    reset_block (rp2040::RESET_PLL_USB);
    unreset_block (rp2040::RESET_PLL_USB);
    reg (b + PLL_CS) = 1;          // REFDIV = 1
    reg (b + PLL_FBDIV_INT) = 100; // VCO = 12 * 100 = 1200 MHz
    reg (b + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
    {
      bool locked = false;
      for (std::uint32_t n = 0; n < 5000000u; ++n)
        {
          if (reg (b + PLL_CS) & PLL_CS_LOCK)
            {
              locked = true;
              break;
            }
        }
      if (!locked)
        {
          return false;
        }
    }
    reg (b + PLL_PRIM) = (5U << 16) | (5U << 12); // /5 /5 = 48 MHz
    reg (b + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;

    // clk_usb = PLL_USB, /1 (AUX-only clock: no glitchless src mux).
    reg (c + CLK_USB_CTRL + CLK_DIV) = CLK_DIV_UNDIVIDED;
    reg (c + CLK_USB_CTRL)
        = (reg (c + CLK_USB_CTRL) & ~CLK_AUX) | (0U << CLK_AUX_LSB);
    reg (c + CLK_USB_CTRL) |= CLK_ENABLE;
    return true;
  }
}
