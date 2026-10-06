/*
 * spi.cpp — SPI1 master driver for the WeAct F411CE, three transports.
 *
 * Polled bytes, an SPI1 RXNE/TXE interrupt transport for medium transfers,
 * and a DMA2 two-stream transport (RX Stream0/Ch3 + TX Stream3/Ch3) for bulk
 * ones. The active transport is chosen at run time by probe(); one image runs
 * on silicon and under QEMU.
 *
 * The three device handlers (SPI1, DMA2 Stream0, DMA2 Stream3) are defined
 * strongly here, overriding the weak Default_Handler aliases in the device
 * vector table. They only post a semaphore, which is legal at NVIC priority 5
 * (masked by the kernel lock, more urgent than PendSV/SysTick).
 */

#include <spi.hpp>

#include <board.hpp>

#include <cmsis-plus/rtos/os.h>

#include "stm32f4xx.h"

using namespace os::rtos;

namespace
{
  // --- one-byte scratch, used when only one side of a transfer is wanted ---

  // --- one-byte scratch, used when only one side of a transfer is wanted ---

  std::uint32_t g_tx_idle_word = 0xFFFF'FFFFU; // DMA TX idle word
  std::uint8_t g_tx_idle_byte = 0xFFU;         // IRQ TX idle byte
  std::uint8_t g_rx_sink;                      // RX of a send-only run

  // --- the active transport -----------------------------------------------

  volatile spi::transport g_transport = spi::transport::polled;

  // --- transfer-complete semaphore, posted by whichever handler finishes ---

  semaphore_binary g_done{ "spi-done", 0 };

  // --- IRQ transport state ------------------------------------------------

  struct transfer_state
  {
    const std::uint8_t* src;
    std::uint8_t* dst;
    bool src_inc;
    bool dst_inc;
    std::size_t len;
    std::size_t sent;
    std::size_t got;
  };

  volatile transfer_state g_xfer{ nullptr, nullptr, false, false, 0, 0, 0 };

  // --- timeouts and spins -------------------------------------------------

  // Roughly two orders above the slowest byte time (16 MHz / 4 prescaler).
  std::uint32_t
  timeout_ticks (std::size_t len)
  {
    return 2U + static_cast<std::uint32_t> (len / 8U);
  }

  // A compile-time-not-removable spin for short delays.
  void
  spin_loop ()
  {
    __asm__ volatile ("" : : : "memory");
  }

  bool
  spin_until (const volatile std::uint32_t* reg, std::uint32_t mask,
              std::uint32_t limit)
  {
    for (std::uint32_t i = 0; i < limit; ++i)
      {
        if ((*reg & mask) == 0U)
          {
            return true;
          }
        spin_loop ();
      }
    return (*reg & mask) == 0U;
  }

  inline void
  drain_busy ()
  {
    // The last bit must have left the wire before CS may rise.
    for (std::uint32_t i = 0; i < 10000U; ++i)
      {
        if ((SPI1->SR & SPI_SR_BSY) == 0U)
          {
            break;
          }
        spin_loop ();
      }
  }

  // --- NVIC / vector helpers ----------------------------------------------

  void
  enable_irq (IRQn_Type irqn)
  {
    // Priority 5: numerically >= the kernel's critical-section level (so the
    // handler is masked while the kernel holds its lock and may signal), and
    // more urgent than PendSV/SysTick (both 15).
    NVIC_SetPriority (irqn, 5);
    NVIC_EnableIRQ (irqn);
    NVIC_ClearPendingIRQ (irqn);
  }

  void
  disable_irq (IRQn_Type irqn)
  {
    NVIC_DisableIRQ (irqn);
    NVIC_ClearPendingIRQ (irqn);
  }

  // --- DMA2 registers (SPI1 RX = Stream0 Ch3, TX = Stream3 Ch3) -----------

  // LIFCR group masks for streams 0 and 3.
  inline constexpr std::uint32_t STREAM0_ALL = 0x3DU; // FEIF0|DMEIF0|TEIF0|HTIF0|TCIF0
  inline constexpr std::uint32_t STREAM3_ALL = 0x3DU << 22U;

  inline constexpr std::uint32_t DMA_COMMON
      = (3U << 25U)   // CHSEL: channel 3
        | (2U << 16U) // PL: high priority
        | (1U << 2U); // TEIE

  enum class outcome
  {
    done,
    untouched,
    partial,
  };

  void
  demote ()
  {
    g_transport = spi::transport::polled;
  }

  // --- DMA2 transport ------------------------------------------------------

  outcome
  dma_run (const std::uint8_t* src, bool src_inc, std::uint8_t* dst,
           bool dst_inc, std::size_t len)
  {
    auto* rx = DMA2_Stream0; // P2M: SPI1_DR -> dst
    auto* tx = DMA2_Stream3; // M2P: src -> SPI1_DR

    // Disable both streams and wait for the disable to land.
    rx->CR &= ~DMA_SxCR_EN;
    tx->CR &= ~DMA_SxCR_EN;
    if (!spin_until (&rx->CR, DMA_SxCR_EN, 10000U)
        || !spin_until (&tx->CR, DMA_SxCR_EN, 10000U))
      {
        return outcome::untouched;
      }

    // Clear all flags for both streams.
    DMA2->LIFCR = STREAM0_ALL | STREAM3_ALL;

    // Drain any stale byte from the SPI data register.
    (void) SPI1->DR;
    (void) SPI1->SR;

    // RX stream: peripheral-to-memory into dst.
    rx->PAR = reinterpret_cast<std::uint32_t> (&SPI1->DR);
    rx->M0AR = dst != nullptr ? reinterpret_cast<std::uint32_t> (dst)
                              : reinterpret_cast<std::uint32_t> (&g_rx_sink);
    rx->NDTR = static_cast<std::uint32_t> (len);
    rx->CR = DMA_COMMON | DMA_SxCR_TCIE | (dst_inc ? DMA_SxCR_MINC : 0U);

    // TX stream: memory-to-peripheral from src (idle word if none given).
    tx->PAR = reinterpret_cast<std::uint32_t> (&SPI1->DR);
    tx->M0AR = src != nullptr
                   ? reinterpret_cast<std::uint32_t> (src)
                   : reinterpret_cast<std::uint32_t> (&g_tx_idle_word);
    tx->NDTR = static_cast<std::uint32_t> (len);
    tx->CR = DMA_COMMON | DMA_SxCR_DIR_0 | (src_inc ? DMA_SxCR_MINC : 0U);

    // Enable RX first, then TX, then the SPI DMA requests. RX armed before TX
    // so the first return frame cannot overrun.
    rx->CR |= DMA_SxCR_EN;
    tx->CR |= DMA_SxCR_EN;
    SPI1->CR2 |= SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;

    bool signalled = (g_done.timed_wait (timeout_ticks (len)) == result::ok);

    // Tear down.
    std::uint32_t remaining = rx->NDTR;
    SPI1->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    tx->CR &= ~DMA_SxCR_EN;
    rx->CR &= ~DMA_SxCR_EN;
    DMA2->LIFCR = STREAM0_ALL | STREAM3_ALL;

    // The interrupt is the discriminator: NDTR at either extreme with no
    // interrupt means nothing moved (QEMU models no DMA2).
    if (signalled && remaining == 0U)
      {
        drain_busy ();
        return outcome::done;
      }
    if (remaining == 0U || remaining == static_cast<std::uint32_t> (len))
      {
        return outcome::untouched;
      }
    return outcome::partial;
  }

  // --- SPI1 IRQ transport --------------------------------------------------

  outcome
  irq_run (const std::uint8_t* src, bool src_inc, std::uint8_t* dst,
           bool dst_inc, std::size_t len)
  {
    {
      interrupts::critical_section cs;
      g_xfer.src = src;
      g_xfer.dst = dst;
      g_xfer.src_inc = src_inc;
      g_xfer.dst_inc = dst_inc;
      g_xfer.len = len;
      g_xfer.sent = 0;
      g_xfer.got = 0;
      // Drain any stale byte.
      (void) SPI1->DR;
      (void) SPI1->SR;
      // TXE is true at idle; enabling TXEIE fires the handler immediately to
      // put the first byte on the wire. TXEIE is used one-shot.
      SPI1->CR2 |= SPI_CR2_TXEIE | SPI_CR2_RXNEIE;
    }

    bool signalled = (g_done.timed_wait (timeout_ticks (len)) == result::ok);

    std::size_t sent, got;
    {
      interrupts::critical_section cs;
      SPI1->CR2 &= ~(SPI_CR2_TXEIE | SPI_CR2_RXNEIE);
      g_xfer.len = 0;
      sent = g_xfer.sent;
      got = g_xfer.got;
    }

    if (signalled && got == len)
      {
        drain_busy ();
        return outcome::done;
      }
    if (sent == 0U)
      {
        return outcome::untouched; // nothing reached the wire
      }
    return outcome::partial;
  }

  // One bulk transfer through the active transport. Returns true if the data
  // was moved (done) or the transport was demoted after a partial move; false
  // means nothing moved and the caller should poll the whole transfer.
  bool
  bulk (const std::uint8_t* tx, std::uint8_t* rx, std::size_t len)
  {
    spi::transport t = g_transport;
    if (t == spi::transport::polled || len < spi::BULK_MIN)
      {
        return false;
      }

    // Drop any stale completion token.
    while (g_done.try_wait () == result::ok)
      {
        ;
      }

    outcome o;
    if (t == spi::transport::dma)
      {
        bool tx_given = (tx != nullptr);
        // DMA runs full-duplex: the missing side goes against a scratch word.
        if (tx_given)
          {
            o = dma_run (tx, true, &g_rx_sink, false, len);
          }
        else
          {
            o = dma_run (reinterpret_cast<const std::uint8_t*> (&g_tx_idle_word),
                         false, rx, true, len);
          }
      }
    else
      {
        bool tx_given = (tx != nullptr);
        if (tx_given)
          {
            o = irq_run (tx, true, const_cast<std::uint8_t*> (&g_rx_sink),
                         false, len);
          }
        else
          {
            o = irq_run (&g_tx_idle_byte, false, rx, true, len);
          }
      }

    switch (o)
      {
      case outcome::done:
        return true;
      case outcome::untouched:
        return false;
      case outcome::partial:
        demote (); // a transfer stopped halfway: never retry, drop to polled
        return true;
      }
    return false;
  }
}

// ---- device interrupt handlers -------------------------------------------
// Strong definitions override the weak Default_Handler aliases in
// device/src/vectors_stm32f411xe.c. Only semaphore::post() and register
// writes, per the device-interrupt rule in the module header.

extern "C" void
SPI1_IRQHandler (void)
{
  auto& x = g_xfer;

  if (x.len > 0 && x.sent == 0U && (SPI1->SR & SPI_SR_TXE) != 0U)
    {
      // First byte, unprompted: TXE is true at idle. sent==0, so src[0].
      SPI1->DR = x.src[0];
      x.sent = 1U;
      SPI1->CR2 &= ~SPI_CR2_TXEIE; // one-shot
    }

  while ((SPI1->SR & SPI_SR_RXNE) != 0U)
    {
      std::uint8_t b = static_cast<std::uint8_t> (SPI1->DR);
      if (x.got >= x.len)
        {
          continue; // stray tail byte; discard
        }
      if (x.dst_inc)
        {
          x.dst[x.got] = b;
        }
      else
        {
          x.dst[0] = b;
        }
      x.got = x.got + 1U;
      if (x.sent < x.len)
        {
          SPI1->DR = x.src[x.src_inc ? x.sent : 0U];
          x.sent = x.sent + 1U;
        }
    }

  if (x.len > 0 && x.got >= x.len)
    {
      SPI1->CR2 &= ~(SPI_CR2_TXEIE | SPI_CR2_RXNEIE);
      x.len = 0;
      (void) g_done.post ();
    }
}

extern "C" void
DMA2_Stream0_IRQHandler (void)
{
  // SPI1 RX stream: transfer-complete is the completion evidence.
  DMA2_Stream0->CR &= ~DMA_SxCR_EN;
  DMA2->LIFCR = STREAM0_ALL;
  (void) g_done.post ();
}

extern "C" void
DMA2_Stream3_IRQHandler (void)
{
  // SPI1 TX stream: no TCIE, so reaching here is a bus/FIFO error. Post so
  // the timeout becomes an immediate partial (and demote).
  DMA2_Stream3->CR &= ~DMA_SxCR_EN;
  DMA2->LIFCR = STREAM3_ALL;
  (void) g_done.post ();
}

// ---- public SPI interface ------------------------------------------------

namespace spi
{
  const char*
  transport_name (transport t)
  {
    switch (t)
      {
      case transport::dma:
        return "dma";
      case transport::irq:
        return "irq";
      case transport::polled:
        return "polled";
      }
    return "?";
  }

  transport
  current ()
  {
    return g_transport;
  }

  void
  set_transport (transport t)
  {
    g_transport = t;
  }

  void
  init (void)
  {
    // Peripheral clocks: GPIOA (AHB1) and SPI1 (APB2).
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void) RCC->APB2ENR;

    const std::uint32_t cs = board::SPI_CS_PIN;

    // PA4 = CS, push-pull output with pull-up, deasserted (high).
    GPIOA->MODER &= ~(0x3U << (cs * 2U));
    GPIOA->MODER |= (0x1U << (cs * 2U));
    GPIOA->OTYPER &= ~(0x1U << cs);
    GPIOA->OSPEEDR |= (0x3U << (cs * 2U));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(0x3U << (cs * 2U))) | (0x1U << (cs * 2U));
    cs_deassert ();

    // PA5=SCK, PA6=MISO, PA7=MOSI: alternate function AF5, push-pull,
    // very-high speed, pull-up (an absent part reads 0xFF on MISO).
    for (std::uint32_t pin = board::SPI_SCK_PIN; pin <= board::SPI_MOSI_PIN;
         ++pin)
      {
        GPIOA->MODER &= ~(0x3U << (pin * 2U));
        GPIOA->MODER |= (0x2U << (pin * 2U)); // AF
        GPIOA->OTYPER &= ~(0x1U << pin);
        GPIOA->OSPEEDR |= (0x3U << (pin * 2U));
        GPIOA->PUPDR = (GPIOA->PUPDR & ~(0x3U << (pin * 2U)))
                       | (0x1U << (pin * 2U));
        // AFR[0] (pins 0..7) nibbles 5..7 = AF5 (SCK=5, MISO=6, MOSI=7).
        const std::uint32_t shift = (pin & 0x7U) * 4U;
        GPIOA->AFR[0] &= ~(0xFU << shift);
        GPIOA->AFR[0] |= (board::SPI_AF5 << shift);
      }
    (void) GPIOA->AFR[0]; // read-back after the AF writes

    // SPI1: master, /4 = 25 MHz, Mode 0, 8-bit, MSB-first. Software NSS
    // (SSI|SSM) so the peripheral never drives PA4. RXONLY deliberately NOT
    // set: transfers are full duplex.
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_BR_0 | SPI_CR1_SSI | SPI_CR1_SSM;
    (void) SPI1->CR1;
    SPI1->CR1 |= SPI_CR1_SPE; // enable last
    (void) SPI1->CR1;
  }

  void
  cs_assert (void)
  {
    GPIOA->BSRR = (1U << (board::SPI_CS_PIN + 16U)); // BR: drive low
  }

  void
  cs_deassert (void)
  {
    GPIOA->BSRR = (1U << board::SPI_CS_PIN); // BS: drive high
  }

  std::uint8_t
  xfer (std::uint8_t data)
  {
    while ((SPI1->SR & SPI_SR_TXE) == 0U)
      {
        ;
      }
    SPI1->DR = data;
    while ((SPI1->SR & SPI_SR_RXNE) == 0U)
      {
        ;
      }
    return static_cast<std::uint8_t> (SPI1->DR);
  }

  void
  send (const std::uint8_t* data, std::size_t len)
  {
    if (!bulk (data, nullptr, len))
      {
        for (std::size_t i = 0; i < len; ++i)
          {
            xfer (data[i]);
          }
      }
  }

  void
  receive (std::uint8_t* out, std::size_t len)
  {
    if (!bulk (nullptr, out, len))
      {
        for (std::size_t i = 0; i < len; ++i)
          {
            out[i] = xfer (0xFF);
          }
      }
  }

  transport
  probe (void)
  {
    // Clock one byte with CS deasserted — nothing is selected, so nothing is
    // issued to the part — and keep the first transport whose completion
    // interrupt actually arrives.
    for (transport cand : { transport::dma, transport::irq })
      {
        if (cand == transport::dma)
          {
            RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
            (void) RCC->AHB1ENR;
            enable_irq (DMA2_Stream0_IRQn);
            enable_irq (DMA2_Stream3_IRQn);
          }
        else
          {
            enable_irq (SPI1_IRQn);
          }

        while (g_done.try_wait () == result::ok)
          {
            ;
          }

        outcome o;
        if (cand == transport::dma)
          {
            o = dma_run (reinterpret_cast<const std::uint8_t*> (&g_tx_idle_word),
                         false, &g_rx_sink, false, 1);
          }
        else
          {
            o = irq_run (&g_tx_idle_byte, false, &g_rx_sink, false, 1);
          }

        if (o == outcome::done)
          {
            g_transport = cand;
            return cand;
          }

        // Not there: mask it so a half-modelled peripheral cannot deliver an
        // interrupt into a driver that has given up on it.
        if (cand == transport::dma)
          {
            disable_irq (DMA2_Stream0_IRQn);
            disable_irq (DMA2_Stream3_IRQn);
          }
        else
          {
            disable_irq (SPI1_IRQn);
          }
      }

    g_transport = transport::polled;
    return transport::polled;
  }
}
