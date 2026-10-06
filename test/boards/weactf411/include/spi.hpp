/*
 * spi.hpp — SPI1 master driver for the WeAct F411CE, with three transports.
 *
 * Pins (shared-loaders WeAct reference): CS=PA4 (push-pull out, pull-up,
 * active low), SCK=PA5 / MISO=PA6 / MOSI=PA7 alternate function AF5,
 * push-pull, very-high speed, pull-up. SPI1 runs on APB2 (= SystemCoreClock,
 * 100 MHz at the PLL configuration); the prescaler is /4, so SCLK = 25 MHz.
 *
 * Three transports behind one send/receive, chosen at run time by probe():
 * DMA2 first, SPI1 IRQ second, polling last. probe() clocks one byte with
 * chip select deasserted and waits for the completion interrupt with a
 * timeout; a transport whose interrupt never arrives is disarmed and the next
 * is tried, so one image runs on both silicon and QEMU.
 *
 * Device interrupt rule (see the board os-app-config.h): these handlers only
 * post a semaphore, so their NVIC priority must be numerically >=
 * OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY (5). Exactly 5 is what
 * the Rust port uses: masked by the kernel lock (so signalling is legal) yet
 * more urgent than PendSV/SysTick (both 15).
 */

#ifndef WEACT_F411CE_SPI_HPP_
#define WEACT_F411CE_SPI_HPP_

#include <cstdint>
#include <cstddef>

namespace spi
{
  // Transfers of this many bytes and more use the interrupt/DMA transport;
  // shorter ones stay polled (a four-byte command header does not repay
  // arming two streams).
  inline constexpr std::size_t BULK_MIN = 16;

  enum class transport
  {
    polled,
    irq,
    dma,
  };

  const char*
  transport_name (transport t);

  // Which transport is active.
  transport
  current ();

  void
  init (void);

  // Chip select.
  void
  cs_assert (void);
  void
  cs_deassert (void);

  // One full-duplex byte, always polled.
  std::uint8_t
  xfer (std::uint8_t data);

  // Send bytes (TX), or fill out (RX). Each uses the active transport when
  // the transfer is big enough and falls back to per-byte polling otherwise.
  void
  send (const std::uint8_t* data, std::size_t len);
  void
  receive (std::uint8_t* out, std::size_t len);

  // Choose the transport: DMA2 if a stream completion arrives, else one SPI1
  // interrupt per byte, else polling. Must run from a thread (needs the
  // scheduler and timed waits).
  transport
  probe (void);

  // Set the transport outright (used to force the polled fallback after a
  // demotion, and by tests).
  void
  set_transport (transport t);
}

#endif /* WEACT_F411CE_SPI_HPP_ */
