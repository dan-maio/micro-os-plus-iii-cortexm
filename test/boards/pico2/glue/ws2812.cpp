/*
 * ws2812.cpp — see ws2812.hpp.
 *
 * WS2812B bit timing (±150 ns tolerated):
 *   '1' : 700 ns high, 600 ns low     '0' : 350 ns high, 800 ns low
 *   reset/latch: >60 µs low (satisfied by the gap between blink updates).
 *
 * We drive the SIO OUT_SET/OUT_CLR registers directly (write-1-to-act, no
 * read-modify-write) and pace the bit phases with deterministic, straight-line
 * NOP delays sized at compile time from clk_sys. NOPs (no branches, no memory
 * reads) give ~1-cycle granularity — essential because the '0' high pulse is
 * only ~53 cycles at 150 MHz, too short for a SysTick/DWT read-loop to time
 * accurately (its per-iteration overhead overshoots and corrupts '0' bits).
 * IRQs are masked for the whole ~30 µs frame, and after the first frame the
 * NOP runs sit in the XIP cache, so timing is stable.
 */

#include <bsp/ws2812.hpp>
#include <bsp/rp2350.hpp>

using rp2350::reg;

#if !defined(PICO2_SYS_CLK_KHZ)
#define PICO2_SYS_CLK_KHZ 150000
#endif

#if !defined(PICO2_WS2812_PIN)
#define PICO2_WS2812_PIN rp2350::LED_PIN
#endif

// Bit timing is sized from this clock. The board ran at a bogus 300 MHz (the
// old clocks.cpp never reset PLL_SYS, so the bootrom's post-dividers were kept),
// which is why the earlier default here was 300000. clocks.cpp now asserts the
// PLL_SYS reset and the core really runs at 150 MHz, so 150000 is the correct
// default. Override with -DWS2812_CLK_KHZ=n if a build's real clk_sys differs.
#if !defined(WS2812_CLK_KHZ)
#define WS2812_CLK_KHZ 150000
#endif

namespace
{
  constexpr std::uint32_t GPIO_FUNC_SIO = 5;
  constexpr std::uint32_t PADS_ISO = 1U << 8, PADS_IE = 1U << 6,
                          PADS_SCHMITT = 1U << 5;

  // ns -> clk_sys cycles at WS2812_CLK_KHZ, rounded to nearest.
  constexpr unsigned
  ns_cycles (unsigned ns)
  {
    return (unsigned) (((std::uint64_t) ns * WS2812_CLK_KHZ + 500000u)
                       / 1000000u);
  }
  // WS2812/WS2812B/SK6812 overlap timings, subtracting loop overheads:
  constexpr unsigned T1H = ns_cycles (900) - 2;
  constexpr unsigned T1L = ns_cycles (350) - 5;
  constexpr unsigned T0H = ns_cycles (500) - 2;
  constexpr unsigned T0L = ns_cycles (750) - 7;

  // Compile-time straight-line NOP delay (exactly N nops, no branches, no call
  // overhead -> exact per-bit timing).
  template <unsigned N> struct nops
  {
    static inline void
    go ()
    {
      __asm__ volatile ("nop");
      nops<N - 1>::go ();
    }
  };
  template <> struct nops<0>
  {
    static inline void
    go ()
    {
    }
  };

  inline void
  configure_pin (std::uint32_t pin)
  {
    volatile std::uint32_t& gpio_ctrl = reg (rp2350::IO_BANK0_BASE + pin * 8 + 4);
    volatile std::uint32_t& pad_ctrl = reg (rp2350::PADS_BANK0_BASE + 0x04 + pin * 4);
    
    // Set 12 mA drive strength (bits 5:4 = 11) and fast slew rate (bit 9 = 1)
    constexpr std::uint32_t PADS_DRIVE_12MA = 3U << 4;
    constexpr std::uint32_t PADS_SLEWFAST = 1U << 9;
    pad_ctrl = (pad_ctrl & ~PADS_ISO) | PADS_IE | PADS_SCHMITT | PADS_DRIVE_12MA | PADS_SLEWFAST;
    
    gpio_ctrl = (gpio_ctrl & ~0x1FU) | GPIO_FUNC_SIO;
    const std::uint32_t half = (pin >= 32u) ? 4u : 0u;
    reg (rp2350::SIO_GPIO_OUT_CLR + half) = 1U << (pin & 31u); // low
    reg (rp2350::SIO_GPIO_OE_SET + half) = 1U << (pin & 31u);  // output enable
  }
}

namespace ws2812
{
  void
  init (void)
  {
    // Configure the default data pin as an SIO output driven low. (set_pin only
    // toggles OUT_SET/OUT_CLR; without this the pad stays in its reset function
    // with the output driver off, so writes never reach the pin.) Bit timing is
    // compile-time NOPs sized for WS2812_CLK_KHZ — no runtime setup needed.
    configure_pin (PICO2_WS2812_PIN);
  }

  std::uint32_t
  default_pin (void)
  {
    return PICO2_WS2812_PIN;
  }

  void
  init_pin (std::uint32_t pin)
  {
    configure_pin (pin);
  }

  // Runs from SRAM (.sram_text): the straight-line NOP bit timing must not be
  // stalled by flash XIP fetch contention (e.g. the other core hammering the
  // flash during boot), or the WS2812 frames are garbled. This is a normal
  // code section (AX), so -g3 compiles cleanly — unlike .data. Projects that
  // want it in SRAM place .sram_text inside the .data output section (copied
  // by boot.S); otherwise it just lives in flash like any other code.
  __attribute__ ((section (".sram_text")))
  void
  set_pin (std::uint32_t pin, std::uint8_t r, std::uint8_t g, std::uint8_t b)
  {
    const std::uint32_t half = (pin >= 32u) ? 4u : 0u;
    const std::uint32_t bit = 1U << (pin & 31u);
    volatile std::uint32_t& set_reg = reg (rp2350::SIO_GPIO_OUT_SET + half);
    volatile std::uint32_t& clr_reg = reg (rp2350::SIO_GPIO_OUT_CLR + half);

    // WS2812 wire order is G, R, B, MSB first.
    const std::uint32_t grb = ((std::uint32_t) g << 16)
                              | ((std::uint32_t) r << 8) | (std::uint32_t) b;

    std::uint32_t pm;
    __asm__ volatile ("mrs %0, primask" : "=r"(pm));
    __asm__ volatile ("cpsid i" ::: "memory");

    for (int i = 23; i >= 0; --i)
      {
        if ((grb >> i) & 1u)
          {
            set_reg = bit;          // '1': ~700 ns high, ~600 ns low
            nops<T1H>::go ();
            clr_reg = bit;
            nops<T1L>::go ();
          }
        else
          {
            set_reg = bit;          // '0': ~320 ns high, ~800 ns low
            nops<T0H>::go ();
            clr_reg = bit;
            nops<T0L>::go ();
          }
      }

    __asm__ volatile ("msr primask, %0" : : "r"(pm) : "memory");
  }

  void
  set (std::uint8_t r, std::uint8_t g, std::uint8_t b)
  {
    set_pin (PICO2_WS2812_PIN, r, g, b);
  }

  std::uint32_t
  measure_clk_khz (void)
  {
    volatile std::uint32_t& RAWL = reg (0x400b0000u + 0x28u); // TIMER0 RAWL, 1MHz
    volatile std::uint32_t& CVR = reg (0xE000E018u);          // SysTick current
    volatile std::uint32_t& RVR = reg (0xE000E014u);          // SysTick reload
    const std::uint32_t period = (RVR & 0x00FFFFFFu) + 1u;

    // TIMER0 only ticks if its TICKS generator is enabled. clocks::init enables
    // only the WATCHDOG tick, and the USB shim enables TIMER0's later — so early
    // callers (os_main, before the USB task) would spin forever on a dead RAWL.
    // Enable TIMER0's tick here (idempotent): 12 clk_ref (12 MHz) cycles = 1 µs.
    reg (rp2350::TICKS_BASE + 0x1Cu) = 12; // TICKS_TIMER0_CYCLES
    reg (rp2350::TICKS_BASE + 0x18u) = 1;  // TICKS_TIMER0_CTRL.ENABLE

    std::uint32_t pm;
    __asm__ volatile ("mrs %0, primask" : "=r"(pm));
    __asm__ volatile ("cpsid i" ::: "memory");

    // All waits are guarded so a stopped TIMER0 can never hang the caller.
    std::uint32_t g;
    std::uint32_t t0 = RAWL;
    for (g = 0; (RAWL - t0) < 2u && g < 2000000u; ++g)
      ; // align to a µs edge
    t0 = RAWL;
    std::uint32_t c0 = CVR & 0x00FFFFFFu;
    for (g = 0; (RAWL - t0) < 100u && g < 2000000u; ++g)
      ; // 100 µs window (<< SysTick period, so no wrap)
    std::uint32_t c1 = CVR & 0x00FFFFFFu;
    std::uint32_t us = RAWL - t0;

    __asm__ volatile ("msr primask, %0" : : "r"(pm) : "memory");

    if (us == 0u)
      return 0u; // TIMER0 not ticking — report 0 rather than hang
    std::uint32_t cyc = (c0 >= c1) ? (c0 - c1) : (c0 + period - c1);
    return (std::uint32_t) (((std::uint64_t) cyc * 1000u) / us);
  }

  std::uint32_t
  selftest (std::uint32_t pin)
  {
    configure_pin (pin);
    const std::uint32_t half = (pin >= 32u) ? 4u : 0u;
    const std::uint32_t bit = 1U << (pin & 31u);
    const std::uint32_t in_off = (pin >= 32u) ? 0x008u : 0x004u; // GPIO_(HI_)IN
    const std::uint32_t oe_off = (pin >= 32u) ? 0x034u : 0x030u; // GPIO_(HI_)OE

    reg (rp2350::SIO_GPIO_OUT_SET + half) = bit; // drive high
    for (volatile int i = 0; i < 200; ++i)
      ;
    std::uint32_t hi = (reg (rp2350::SIO_BASE + in_off) >> (pin & 31u)) & 1u;

    reg (rp2350::SIO_GPIO_OUT_CLR + half) = bit; // drive low
    for (volatile int i = 0; i < 200; ++i)
      ;
    std::uint32_t lo = (reg (rp2350::SIO_BASE + in_off) >> (pin & 31u)) & 1u;

    std::uint32_t oe = (reg (rp2350::SIO_BASE + oe_off) >> (pin & 31u)) & 1u;
    std::uint32_t func = reg (rp2350::IO_BANK0_BASE + pin * 8 + 4) & 0x1Fu;
    std::uint32_t pad = reg (rp2350::PADS_BANK0_BASE + 0x04 + pin * 4) & 0xFFFFu;

    return (hi << 0) | (lo << 1) | (oe << 2) | ((func & 0x1Fu) << 8)
           | (pad << 16);
  }
}
