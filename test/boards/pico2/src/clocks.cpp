/*
 * clocks.cpp — RP2350 clock configuration (XOSC + PLL_SYS → 150 MHz).
 *
 * Faithful C++ port of the working Eclipse reference (reference/src/clocks.c).
 * Sequence: start XOSC(12 MHz) → move clk_sys/clk_ref off any AUX mux →
 * PLL_SYS VCO = 12*125 = 1500 MHz, /5 /2 = 150 MHz → clk_ref=XOSC,
 * clk_sys=PLL_SYS, clk_peri=clk_sys.
 */

#include <bsp/clocks.hpp>
#include <bsp/rp2350.hpp>

using rp2350::reg;

namespace
{
  // Register offsets.
  constexpr std::uint32_t XOSC_CTRL = 0x00, XOSC_STATUS = 0x04, XOSC_STARTUP = 0x0C;
  constexpr std::uint32_t PLL_CS = 0x00, PLL_PWR = 0x04, PLL_FBDIV_INT = 0x08,
                          PLL_PRIM = 0x0C;
  constexpr std::uint32_t CLK_REF_CTRL = 0x30, CLK_SYS_CTRL = 0x3C,
                          CLK_PERI_CTRL = 0x48;
  constexpr std::uint32_t CLK_DIV = 0x04, CLK_SELECTED = 0x08;
  constexpr std::uint32_t RESETS_RESET = 0x00, RESETS_DONE = 0x08;

  // Bit fields.
  constexpr std::uint32_t XOSC_FREQ_1_15MHZ = 0x0AA0, XOSC_ENABLE = 0x0FAB;
  constexpr std::uint32_t XOSC_STABLE = 1U << 31;
  constexpr std::uint32_t PLL_PWR_PD = 1U << 0, PLL_PWR_POSTDIVPD = 1U << 3,
                          PLL_PWR_VCOPD = 1U << 5, PLL_CS_LOCK = 1U << 31;
  constexpr std::uint32_t CLK_REF_SRC = 0x3, CLK_REF_SRC_XOSC = 2;
  constexpr std::uint32_t CLK_SYS_SRC = 0x3, CLK_SYS_SRC_AUX = 1;
  constexpr std::uint32_t CLK_SYS_AUX = 0xE0, CLK_SYS_AUX_LSB = 5;
  constexpr std::uint32_t CLK_REF_AUX = 0x60, CLK_REF_AUX_LSB = 5;
  constexpr std::uint32_t CLK_PERI_AUX = 0xE0, CLK_PERI_AUX_LSB = 5,
                          CLK_PERI_ENABLE = 1U << 11;
  constexpr std::uint32_t RESET_PLL_SYS = 1U << 14, RESET_IO_BANK0 = 1U << 6,
                          RESET_PADS_BANK0 = 1U << 9;
  constexpr std::uint32_t CLK_DIV_UNDIVIDED = 1U << 16;

  void
  reset_block (std::uint32_t mask)
  {
    reg (rp2350::RESETS_BASE + RESETS_RESET) |= mask;
    // Wait a couple of cycles for the reset to propagate
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");
  }

  void
  unreset_block (std::uint32_t mask)
  {
    reg (rp2350::RESETS_BASE + RESETS_RESET) &= ~mask;
    while ((reg (rp2350::RESETS_BASE + RESETS_DONE) & mask) != mask)
      {
      }
  }

  void
  xosc_init (void)
  {
    reg (rp2350::XOSC_BASE + XOSC_STARTUP) = 0xC4;
    reg (rp2350::XOSC_BASE + XOSC_CTRL) = (XOSC_ENABLE << 12) | XOSC_FREQ_1_15MHZ;
    while (!(reg (rp2350::XOSC_BASE + XOSC_STATUS) & XOSC_STABLE))
      {
      }
  }

  // Program PLL_SYS to VCO = 12 MHz * fbdiv, then /postdiv1 /postdiv2.
  void
  pll_sys_program (std::uint32_t fbdiv, std::uint32_t postdiv1,
                   std::uint32_t postdiv2)
  {
    const std::uint32_t b = rp2350::PLL_SYS_BASE;
    reset_block (RESET_PLL_SYS);
    unreset_block (RESET_PLL_SYS);
    reg (b + PLL_CS) = 1;          // REFDIV = 1
    reg (b + PLL_FBDIV_INT) = fbdiv;
    reg (b + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
    while (!(reg (b + PLL_CS) & PLL_CS_LOCK))
      {
      }
    reg (b + PLL_PRIM) = (postdiv1 << 16) | (postdiv2 << 12);
    reg (b + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;
  }

  // Compile-time default clock for builds that do NOT read the scratch table.
#if !defined(PICO2_SYS_CLK_KHZ)
#define PICO2_SYS_CLK_KHZ 150000
#endif
#if PICO2_SYS_CLK_KHZ == 150000
  constexpr std::uint32_t CT_FBDIV = 125, CT_PD1 = 5, CT_PD2 = 2; // VCO 1500
  constexpr std::uint8_t  CT_VSEL  = 0x0b;                       // 1.10 V
#elif PICO2_SYS_CLK_KHZ == 120000
  constexpr std::uint32_t CT_FBDIV = 100, CT_PD1 = 5, CT_PD2 = 2; // VCO 1200
  constexpr std::uint8_t  CT_VSEL  = 0x0b;                        // 1.10 V
#elif PICO2_SYS_CLK_KHZ == 100000
  constexpr std::uint32_t CT_FBDIV = 125, CT_PD1 = 5, CT_PD2 = 3; // VCO 1500
  constexpr std::uint8_t  CT_VSEL  = 0x0b;                        // 1.10 V
#elif PICO2_SYS_CLK_KHZ == 200000
  constexpr std::uint32_t CT_FBDIV = 100, CT_PD1 = 6, CT_PD2 = 1; // VCO 1200
  constexpr std::uint8_t  CT_VSEL  = 0x0d;                        // 1.20 V
#elif PICO2_SYS_CLK_KHZ == 250000
  constexpr std::uint32_t CT_FBDIV = 125, CT_PD1 = 6, CT_PD2 = 1; // VCO 1500
  constexpr std::uint8_t  CT_VSEL  = 0x0e;                        // 1.25 V
#elif PICO2_SYS_CLK_KHZ == 48000
  constexpr std::uint32_t CT_FBDIV = 100, CT_PD1 = 5, CT_PD2 = 5; // VCO 1200
  constexpr std::uint8_t  CT_VSEL  = 0x0b;                        // 1.10 V
#else
#error "unsupported PICO2_SYS_CLK_KHZ (250000 / 200000 / 150000 / 120000 / 100000 / 48000)"
#endif

  // Configure a glitchless clock (clk_ref / clk_sys).
  void
  clk_configure (std::uint32_t ctrl_addr, std::uint32_t src_mask,
                 std::uint32_t aux_mask, std::uint32_t aux_lsb, std::uint32_t src,
                 std::uint32_t auxsrc)
  {
    if (src == CLK_SYS_SRC_AUX)
      {
        reg (ctrl_addr) &= ~src_mask;
        while (!(reg (ctrl_addr + CLK_SELECTED) & 1U))
          {
          }
      }
    reg (ctrl_addr) = (reg (ctrl_addr) & ~aux_mask) | (auxsrc << aux_lsb);
    reg (ctrl_addr) = (reg (ctrl_addr) & ~src_mask) | (src << 0);
    while (!(reg (ctrl_addr + CLK_SELECTED) & (1U << src)))
      {
      }
    reg (ctrl_addr + CLK_DIV) = CLK_DIV_UNDIVIDED;
  }
}

namespace
{
  // Selected-clock facts, published by init() for current_*().
  std::uint32_t g_khz = PICO2_SYS_CLK_KHZ;
  std::uint8_t  g_vsel = 0x0b;   // 1.10 V default
  unsigned      g_index = 0xFFFFu;

  // Bring clk_sys = clk_peri = PLL_SYS(fbdiv/pd1/pd2), clk_ref = XOSC.
  void
  bring_up_clock_tree (std::uint32_t fbdiv, std::uint32_t pd1,
                       std::uint32_t pd2)
  {
    const std::uint32_t c = rp2350::CLOCKS_BASE;

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

    pll_sys_program (fbdiv, pd1, pd2);

    // clk_ref = XOSC (12 MHz)
    clk_configure (c + CLK_REF_CTRL, CLK_REF_SRC, CLK_REF_AUX, CLK_REF_AUX_LSB,
                   CLK_REF_SRC_XOSC, 0);
    // clk_sys = PLL_SYS
    clk_configure (c + CLK_SYS_CTRL, CLK_SYS_SRC, CLK_SYS_AUX, CLK_SYS_AUX_LSB,
                   CLK_SYS_SRC_AUX, 0 /* AUXSRC_PLL_SYS */);

    // clk_peri = clk_sys — UART reference clock.
    reg (c + CLK_PERI_CTRL) &= ~CLK_PERI_ENABLE;
    reg (c + CLK_PERI_CTRL)
        = (reg (c + CLK_PERI_CTRL) & ~CLK_PERI_AUX) | (0U << CLK_PERI_AUX_LSB);
    reg (c + CLK_PERI_CTRL + CLK_DIV) = CLK_DIV_UNDIVIDED;
    reg (c + CLK_PERI_CTRL) |= CLK_PERI_ENABLE;

    unreset_block (RESET_IO_BANK0 | RESET_PADS_BANK0);

    // Enable watchdog and TIMER0 tick generators centrally in the TICKS block.
    // 12 clk_ref (12 MHz) cycles = 1 µs per tick.
    reg (rp2350::TICKS_BASE + 0x34) = 12;   // TICKS_WATCHDOG_CYCLES
    reg (rp2350::TICKS_BASE + 0x30) = 1;    // TICKS_WATCHDOG_CTRL.ENABLE
    reg (rp2350::TICKS_BASE + 0x1C) = 12;   // TICKS_TIMER0_CYCLES
    reg (rp2350::TICKS_BASE + 0x18) = 1;    // TICKS_TIMER0_CTRL.ENABLE
  }
}

// SystemCoreClock lives in device/src/system_rp2350.c; init() updates it so the
// UART baud divisor and SysTick reload track the chosen frequency.
extern "C" std::uint32_t SystemCoreClock;

namespace clocks
{
  void
  init (void)
  {
#if defined(PICO2_CLOCK_FROM_SCRATCH)
    // Table-driven: pick the row from watchdog scratch (default 150 MHz on a
    // cold boot). Raise Vcore BEFORE the clock; we are still on the slow boot
    // clock here, so any valid voltage is safe.
    constexpr unsigned DEFAULT_INDEX = 0;   // 48 MHz (pot rests at 0 on power-up)
    constexpr std::uint32_t MAGIC = 0xC10C4B01u;
    unsigned idx = DEFAULT_INDEX;
    if (reg (rp2350::WATCHDOG_BASE + 0x10) == MAGIC) // SCRATCH1
      {
        std::uint32_t s = reg (rp2350::WATCHDOG_BASE + 0x0C); // SCRATCH0
        if (s < SYS_TABLE_LEN)
          idx = static_cast<unsigned> (s);
      }
    const sys_entry& e = SYS_TABLE[idx];
    vreg_set (e.vsel);
#ifdef PICO2_ENABLE_250MHZ
    // Overclock (>200 MHz): the XIP flash SCK = clk_sys / QMI CLKDIV. The bootrom
    // fixed CLKDIV for its default clock, so at 250 MHz the flash would be read
    // too fast. Raise CLKDIV so SCK stays ~<=90 MHz. Safe to do here: we are still
    // on the slow boot clock, and RAISING the divider only slows flash reads.
    if (e.khz > 200000u)
      {
        constexpr std::uint32_t QMI_M0_TIMING = 0x400D000Cu; // CLKDIV = bits[7:0]
        std::uint32_t div = (e.khz + 89999u) / 90000u;       // 250 MHz -> 3
        if (div < 2u)
          div = 2u;
        reg (QMI_M0_TIMING) = (reg (QMI_M0_TIMING) & ~0xFFu) | (div & 0xFFu);
#ifdef PICO2_PSRAM
        // Same for the external QSPI PSRAM (QMI M1): its SCK = clk_sys / CLKDIV
        // too, and 250/2 = 125 MHz would exceed the APS6404's ~109 MHz window.
        constexpr std::uint32_t QMI_M1_TIMING = 0x400D0020u;
        reg (QMI_M1_TIMING) = (reg (QMI_M1_TIMING) & ~0xFFu) | (div & 0xFFu);
#endif
        __asm__ volatile ("dsb 0xf" ::: "memory");
      }
#endif
    bring_up_clock_tree (e.fbdiv, e.pd1, e.pd2);
    g_index = idx;
    g_khz = e.khz;
    g_vsel = e.vsel;
    SystemCoreClock = e.khz * 1000u;
#else
    // Flash M0 SCK = clk_sys / M0_CLKDIV. The bootrom fixed M0_CLKDIV for its
    // boot clock; when we raise clk_sys above 48 MHz the flash XIP would be
    // clocked too fast (this board's flash+PSRAM coexistence was proven at a
    // ~48 MHz flash SCK). Hold flash SCK <= ~50 MHz by raising M0_CLKDIV to
    // ceil(clk_sys / 50 MHz). Safe here: still on the slow boot clock, and
    // raising the divider only slows flash reads. PSRAM (M1) keeps its own
    // CLKDIV from psram::init().
#if PICO2_SYS_CLK_KHZ > 48000
    {
      constexpr std::uint32_t QMI_M0_TIMING = 0x400D000Cu; // CLKDIV = bits[7:0]
      std::uint32_t div = (PICO2_SYS_CLK_KHZ + 49999u) / 50000u; // 100->2 150->3
      if (div < 1u)
        div = 1u;
      reg (QMI_M0_TIMING) = (reg (QMI_M0_TIMING) & ~0xFFu) | (div & 0xFFu);
      __asm__ volatile ("dsb 0xf" ::: "memory");
    }
#endif
    // Overclocking (>= 200 MHz) needs a higher core rail: raise Vcore BEFORE
    // the clock, while we are still on the slow boot clock.
    if (CT_VSEL > 0x0b)
      {
        vreg_set (CT_VSEL);
      }
    bring_up_clock_tree (CT_FBDIV, CT_PD1, CT_PD2);
    g_khz = PICO2_SYS_CLK_KHZ;
    g_vsel = CT_VSEL;
    SystemCoreClock = PICO2_SYS_CLK_KHZ * 1000u;
#endif
  }

  unsigned      current_index (void) { return g_index; }
  std::uint32_t current_khz (void)   { return g_khz; }
  std::uint8_t  current_vsel (void)  { return g_vsel; }

  // Bring up the 48 MHz USB clock (PLL_USB + clk_usb). Required by the USB
  // controller (usb-hid driver); not part of the default clock tree. Call once
  // before tusb_init(). Returns true if PLL_USB locked and clk_usb is running.
  bool
  usb_init (void)
  {
    constexpr std::uint32_t PLL_USB_BASE = 0x40058000u;
    constexpr std::uint32_t RESET_PLL_USB = 1U << 15;
    constexpr std::uint32_t RESET_USBCTRL = 1U << 28;
    const std::uint32_t b = PLL_USB_BASE;
    const std::uint32_t c = rp2350::CLOCKS_BASE;
    constexpr std::uint32_t CLK_USB_CTRL = 0x60, CLK_USB_ENABLE = 1U << 11,
                            CLK_USB_AUX = 0xE0, CLK_USB_AUX_LSB = 5;

    // Hold the USB controller in reset while we reprogram PLL_USB / clk_usb.
    // On a warm reset the controller may still be running the previous boot's
    // configuration; a glitched or dropped clk_usb then desyncs its clock
    // domain. tusb_init() releases it afterwards (rp2usb_init reset+unreset).
    reset_block (RESET_USBCTRL);

    // Take clk_usb down first: reprogramming PLL_USB underneath a live clk_usb
    // glitches the USB controller's 48 MHz clock.
    reg (c + CLK_USB_CTRL) &= ~CLK_USB_ENABLE;

    // Power-cycle PLL_USB so the register writes below always take effect. A
    // warm reset leaves PLL_USB running from the previous boot, and writing
    // FBDIV/PRIM to a live, locked PLL is IGNORED by the hardware — so without
    // the reset the PLL never re-locks, clk_usb stays dead and USB enumerates
    // only on the first (cold) boot. Same trap the PLL_SYS reset fixes.
    reset_block (RESET_PLL_USB);
    unreset_block (RESET_PLL_USB);
    reg (b + PLL_CS) = 1;           // REFDIV = 1
    reg (b + PLL_FBDIV_INT) = 100;  // VCO = 12 * 100 = 1200 MHz
    reg (b + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
    // Bounded lock wait: on a warm reset the PLL can occasionally be slow to
    // re-lock; never hang the USB task here — return false so the caller can
    // power-cycle PLL_USB again and retry.
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
        = (reg (c + CLK_USB_CTRL) & ~CLK_USB_AUX) | (0U << CLK_USB_AUX_LSB);
    reg (c + CLK_USB_CTRL) |= CLK_USB_ENABLE;
    return true;
  }

  // ---- Runtime frequency table (48..200 MHz) ------------------------------
  // clk_sys = 12 MHz * fbdiv / pd1 / pd2. VCO = 12*fbdiv (kept 750..1600 MHz).
  // Every row is <0.03 % from 115200 baud with the PL011 fractional divisor
  // (IBRD/FBRD are recomputed from SystemCoreClock every boot).
  // vsel: 0x0b=1.10 V (<=150 MHz), 0x0c=1.15 V (176/192), 0x0d=1.20 V (200).
  const sys_entry SYS_TABLE[SYS_TABLE_LEN] = {
    {  48000, 100, 5, 5, 0x0b }, // VCO 1200
    {  60000, 125, 5, 5, 0x0b }, // VCO 1500
    {  72000, 120, 5, 4, 0x0b }, // VCO 1440
    {  84000,  98, 7, 2, 0x0b }, // VCO 1176
    {  96000,  96, 4, 3, 0x0b }, // VCO 1152
    { 108000, 108, 6, 2, 0x0b }, // VCO 1296
    { 120000, 100, 5, 2, 0x0b }, // VCO 1200
    { 132000, 110, 5, 2, 0x0b }, // VCO 1320
    { 150000, 125, 5, 2, 0x0b }, // VCO 1500 (default)
    { 176000,  88, 6, 1, 0x0c }, // VCO 1056, 1.15 V
    { 192000,  96, 6, 1, 0x0c }, // VCO 1152, 1.15 V
    { 200000, 100, 6, 1, 0x0d }, // VCO 1200, 1.20 V
#ifdef PICO2_ENABLE_250MHZ
    { 250000, 125, 6, 1, 0x0e }, // VCO 1500, 1.25 V — EXPERIMENTAL overclock
#endif
  };

  std::uint32_t
  vsel_to_mv (std::uint8_t vsel)
  {
    // POWMAN VSEL codes 0x00..0x0f: 0.55 V + 50 mV per step (0x0b = 1.10 V,
    // 0x0f = 1.30 V). Only the low range is used here.
    return 550u + 50u * static_cast<std::uint32_t> (vsel);
  }

  void
  vreg_set (std::uint8_t vsel)
  {
    constexpr std::uint32_t PASSWD = 0x5AFE0000u;
    constexpr std::uint32_t VREG_CTRL = 0x04, VREG = 0x0C;
    constexpr std::uint32_t UNLOCK = 1U << 13;
    constexpr std::uint32_t VSEL_LSB = 4, VSEL_BITS = 0x1F0u;
    constexpr std::uint32_t UPDATE_IN_PROGRESS = 1U << 15;
    const std::uint32_t p = rp2350::POWMAN_BASE;

    // Unlock the regulator (needed only for >1.30 V, harmless otherwise).
    reg (p + VREG_CTRL) = PASSWD | (reg (p + VREG_CTRL) & 0xFFFFu) | UNLOCK;
    while (reg (p + VREG) & UPDATE_IN_PROGRESS)
      {
      }
    // Write VSEL (password in the high half on every POWMAN write).
    reg (p + VREG) = PASSWD
                     | (reg (p + VREG) & ~VSEL_BITS)
                     | (static_cast<std::uint32_t> (vsel) << VSEL_LSB);
    while (reg (p + VREG) & UPDATE_IN_PROGRESS)
      {
      }
    // Let the rail settle before the caller raises the clock (~a few hundred µs
    // worst case; we are on the slow boot/XOSC clock here).
    for (int i = 0; i < 20000; ++i)
      {
        __asm__ volatile ("nop");
      }
  }

  void
  adc_clk_init (void)
  {
    // Bring up PLL_USB (48 MHz) if not already, then route clk_adc from it.
    const std::uint32_t b = rp2350::PLL_USB_BASE;
    constexpr std::uint32_t RESET_PLL_USB = 1U << 15;
    unreset_block (RESET_PLL_USB);
    if (!(reg (b + PLL_CS) & PLL_CS_LOCK))
      {
        reg (b + PLL_CS) = 1;
        reg (b + PLL_FBDIV_INT) = 100; // VCO 1200
        reg (b + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
        while (!(reg (b + PLL_CS) & PLL_CS_LOCK))
          {
          }
        reg (b + PLL_PRIM) = (5U << 16) | (5U << 12); // /5 /5 = 48 MHz
        reg (b + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;
      }

    // clk_adc = PLL_USB (AUXSRC 0), /1, enable. clk_adc CTRL @0x6c.
    const std::uint32_t c = rp2350::CLOCKS_BASE;
    constexpr std::uint32_t CLK_ADC_CTRL = 0x6C, CLK_ADC_ENABLE = 1U << 11,
                            CLK_ADC_AUX = 0xE0, CLK_ADC_AUX_LSB = 5;
    reg (c + CLK_ADC_CTRL) &= ~CLK_ADC_ENABLE;
    reg (c + CLK_ADC_CTRL + CLK_DIV) = CLK_DIV_UNDIVIDED;
    reg (c + CLK_ADC_CTRL)
        = (reg (c + CLK_ADC_CTRL) & ~CLK_ADC_AUX) | (0U << CLK_ADC_AUX_LSB);
    reg (c + CLK_ADC_CTRL) |= CLK_ADC_ENABLE;
  }

  [[noreturn]] void
  reboot_to_index (unsigned idx)
  {
    constexpr std::uint32_t MAGIC = 0xC10C4B01u;
    const std::uint32_t w = rp2350::WATCHDOG_BASE;

    // Quiesce BEFORE arming the watchdog. The reset has a short countdown window;
    // if the OTHER core keeps running during it (driving the console at the
    // current baud, or holding a QMI/PSRAM transaction), the reboot intermittently
    // came out as UART garbage / a wedged boot. Mask this core's IRQs and force
    // the other core into reset (PSM FRCE_OFF) so nothing touches the bus while we
    // arm and count down. (PSM bits: PROC0 = 1<<23, PROC1 = 1<<24.)
    __asm__ volatile ("cpsid i" ::: "memory");
    {
      const std::uint32_t self = reg (rp2350::SIO_CPUID) & 1u;
      const std::uint32_t other_proc = self ? (1u << 23) : (1u << 24);
      reg (rp2350::PSM_BASE + 0x04) |= other_proc;   // FRCE_OFF: hold other core
      for (int i = 0; i < 2000; ++i)                 // let the reset assert
        {
          __asm__ volatile ("nop");
        }
    }

    reg (w + 0x0C) = idx;    // SCRATCH0 = target index
    reg (w + 0x10) = MAGIC;  // SCRATCH1 = validity magic
    reg (w + 0x1C) = 0;      // SCRATCH4 = 0: no bootrom reboot-vector, boot flash

    // The watchdog counts on its TICK; without the tick running a LOAD countdown
    // (or even TRIGGER, on some silicon) never fires. Enable it: 12 clk_ref
    // (12 MHz) cycles = 1 µs per tick.
    reg (rp2350::TICKS_BASE + 0x34) = 12;   // TICKS_WATCHDOG_CYCLES
    reg (rp2350::TICKS_BASE + 0x30) = 1;    // TICKS_WATCHDOG_CTRL.ENABLE

    // Watchdog resets everything except ROSC/XOSC (PSM WDSEL @0x08).
    reg (rp2350::PSM_BASE + 0x08) = 0x01FFFFFFu & ~0x0Cu;

    // CTRL: ENABLE off, and clear the debug/JTAG PAUSE bits — with a debugger
    // (OpenOCD) attached those pause the watchdog so it never resets.
    std::uint32_t ctrl = reg (w + 0x00);
    ctrl &= ~(1U << 30);                             // ENABLE = 0
    ctrl &= ~((1U << 26) | (1U << 25) | (1U << 24)); // PAUSE_DBG1/DBG0/JTAG = 0
    reg (w + 0x00) = ctrl;
    // Short countdown (~2 ms of ticks), then ENABLE -> the watchdog resets us.
    reg (w + 0x04) = 2000;                           // LOAD (µs ticks)
    reg (w + 0x00) = ctrl | (1U << 30);             // CTRL.ENABLE
    for (;;)
      {
        __asm__ volatile ("wfi");
      }
  }
}
