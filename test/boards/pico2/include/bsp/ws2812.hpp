/*
 * ws2812.hpp — general bit-banged WS2812 / NeoPixel driver for the RP2350
 * (pico2 BSP). For boards whose onboard LED is an addressable RGB rather than a
 * plain GPIO LED (e.g. the Spotpear RP2350B-MINI-A).
 *
 * Bit timing uses deterministic, straight-line NOP delays sized at compile time
 * from clk_sys (PICO2_SYS_CLK_KHZ, default 150 MHz), with IRQs masked for the
 * frame. This needs no debugger (unlike DWT CYCCNT — the RP2350B-MINI-A has no
 * debug pins) and, unlike a SysTick/DWT read-loop, has ~1-cycle granularity,
 * which the WS2812's ~350 ns '0' high pulse requires.
 *
 * The default data pin is PICO2_WS2812_PIN (or the port's LED_PIN if unset),
 * but the runtime-pin variants let a scan probe several GPIOs to find the one
 * the board's RGB is wired to.
 */
#ifndef PICO2_WS2812_HPP_
#define PICO2_WS2812_HPP_

#include <cstdint>

#if defined(PICO2_HAS_WS2812)
namespace ws2812
{
  // One-time: enable the DWT cycle counter (with fallback probe).
  void init (void);

  // Configure `pin` as an SIO output driven low (call before set on that pin).
  void init_pin (std::uint32_t pin);

  // Send one pixel to the given pin. Wire order is WS2812 G,R,B. IRQs are
  // masked for the ~30 µs frame.
  void set_pin (std::uint32_t pin, std::uint8_t r, std::uint8_t g,
                std::uint8_t b);

  // Convenience wrappers on the compile-time default pin.
  void set (std::uint8_t r, std::uint8_t g, std::uint8_t b);
  std::uint32_t default_pin (void);

  // Diagnostic: configure `pin`, drive it high then low, and read the pin back
  // via SIO GPIO_IN. Returns a packed result:
  //   bit0  = GPIO_IN while driven HIGH  (want 1)
  //   bit1  = GPIO_IN while driven LOW   (want 0)
  //   bit2  = SIO GPIO_OE bit for pin    (want 1 = output enabled)
  //   [12:8]  = IO_BANK0 CTRL FUNCSEL    (want 5 = SIO)
  //   [31:16] = PADS_BANK0 pad register (IE=bit6, OD=bit7, ISO=bit8)
  std::uint32_t selftest (std::uint32_t pin);

  // Measure the actual clk_sys in kHz against the 1 MHz TIMER (SysTick counts
  // clk_sys cycles). Used to confirm the NOP bit-timing base. Expect ~150000.
  std::uint32_t measure_clk_khz (void);
}
#else
// Boards without an RGB (WS2812) LED: provide no-op stubs so code that would
// use the fault beacon (rtos-glue) still links without compiling ws2812.cpp.
namespace ws2812
{
  inline void init (void) {}
  inline void init_pin (std::uint32_t) {}
  inline void set_pin (std::uint32_t, std::uint8_t, std::uint8_t,
                       std::uint8_t) {}
  inline void set (std::uint8_t, std::uint8_t, std::uint8_t) {}
  inline std::uint32_t default_pin (void) { return 0; }
  inline std::uint32_t selftest (std::uint32_t) { return 0; }
  inline std::uint32_t measure_clk_khz (void) { return 0; }
}
#endif

#endif /* PICO2_WS2812_HPP_ */
