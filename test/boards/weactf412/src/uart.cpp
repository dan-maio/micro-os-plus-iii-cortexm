/*
 * uart.cpp — USART1 console driver for the WeAct F412RE (PA9/PA10).
 *
 * PA9 = USART1_TX, PA10 = USART1_RX, alternate function AF7. USART1 lives on
 * APB2, which in the BSP clock tree (AHB /1, APB2 /1) runs at the same
 * 100 MHz as SystemCoreClock, so the baud divisor is computed from that.
 *
 * RX: the RXNE interrupt pushes bytes into a 256-byte ring and posts
 *     g_rx_ready; uart::read_char() waits on it.
 * TX: a 512-byte ring is drained by DMA2 Stream7 (channel 4). The
 *     transfer-complete interrupt advances the tail, posts g_tx_free and
 *     starts the next contiguous chunk. uart::put_char() waits on g_tx_free.
 *
 * The two device handlers are strong definitions overriding the weak
 * Default_Handler aliases in device/src/vectors_stm32f412rx.c. They run at
 * NVIC priority 5 (masked by the kernel lock, more urgent than PendSV/SysTick)
 * and only touch registers and post semaphores.
 */

#include <uart.hpp>

#include <board.hpp>

#include <cmsis-plus/rtos/os.h>

#include "stm32f4xx.h"

using namespace os::rtos;

namespace
{
  constexpr std::size_t TX_SIZE = 512U;
  constexpr std::size_t RX_SIZE = 256U;

  std::uint8_t g_tx_buf[TX_SIZE];
  volatile std::size_t g_tx_head = 0U;
  volatile std::size_t g_tx_tail = 0U;
  volatile std::size_t g_tx_chunk = 0U;
  volatile bool g_tx_active = false;

  std::uint8_t g_rx_buf[RX_SIZE];
  volatile std::size_t g_rx_head = 0U;
  volatile std::size_t g_rx_tail = 0U;

  // Free slots in the TX ring (producer waits) and received bytes (consumer
  // waits). Posted by the device interrupt handlers.
  semaphore_counting g_tx_free{ "uart-tx-free", TX_SIZE - 1U, TX_SIZE - 1U };
  semaphore_counting g_rx_ready{ "uart-rx-ready", RX_SIZE, 0U };

  inline std::size_t
  ring_next (std::size_t i, std::size_t size)
  {
    const std::size_t n = i + 1U;
    return (n == size) ? 0U : n;
  }

  void
  gpio_config_af (std::uint32_t pin, std::uint32_t af)
  {
    // MODER: 10 = alternate function.
    GPIOA->MODER &= ~(0x3U << (pin * 2U));
    GPIOA->MODER |= (0x2U << (pin * 2U));

    // OTYPER push-pull, high speed, no pull.
    GPIOA->OTYPER &= ~(0x1U << pin);
    GPIOA->OSPEEDR |= (0x3U << (pin * 2U));
    GPIOA->PUPDR &= ~(0x3U << (pin * 2U));

    // AFR[0] for pins 0..7, AFR[1] for pins 8..15.
    volatile std::uint32_t& afr = GPIOA->AFR[pin >> 3U];
    const std::uint32_t shift = (pin & 0x7U) * 4U;
    afr &= ~(0xFU << shift);
    afr |= (af << shift);
  }

  // Start a DMA transfer for the contiguous chunk tail..head (or tail..end).
  // The caller must hold a critical section.
  void
  tx_start (void)
  {
    if (g_tx_active || (g_tx_tail == g_tx_head))
      {
        return;
      }

    const std::size_t len = (g_tx_head > g_tx_tail)
        ? (g_tx_head - g_tx_tail)
        : (TX_SIZE - g_tx_tail);

    g_tx_chunk = len;
    g_tx_active = true;

    DMA2_Stream7->CR &= ~DMA_SxCR_EN;
    while ((DMA2_Stream7->CR & DMA_SxCR_EN) != 0U)
      {
      }
    DMA2_Stream7->M0AR = reinterpret_cast<std::uint32_t> (&g_tx_buf[g_tx_tail]);
    DMA2_Stream7->NDTR = len;
    DMA2_Stream7->CR |= DMA_SxCR_EN;
  }

  void
  irq_enable (IRQn_Type irqn)
  {
    // Priority 5: numerically >= the kernel's critical-section level (so the
    // handler is masked while the kernel holds its lock and may signal), and
    // more urgent than PendSV/SysTick (both 15).
    NVIC_SetPriority (irqn, 5);
    NVIC_EnableIRQ (irqn);
    NVIC_ClearPendingIRQ (irqn);
  }

  int
  rx_pop (void)
  {
    __disable_irq ();
    const std::uint8_t b = g_rx_buf[g_rx_tail];
    g_rx_tail = ring_next (g_rx_tail, RX_SIZE);
    __enable_irq ();
    return static_cast<int> (b);
  }
}

namespace uart
{
  void
  init (std::uint32_t baud)
  {
    // Peripheral clocks: GPIOA (AHB1), DMA2 (AHB1) and USART1 (APB2).
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void) RCC->APB2ENR;

    gpio_config_af (board::UART_TX_PIN, board::UART_AF7);
    gpio_config_af (board::UART_RX_PIN, board::UART_AF7);

    USART1->CR1 = 0U; // disable while configuring

    // BRR = round(f_apb2 / baud); with OVER8=0 the whole register is the
    // USARTDIV value in 12.4 fixed point, which equals f/baud. USART1 is on
    // APB2 and the BSP keeps APB2 at SystemCoreClock (100 MHz).
    const std::uint32_t f_apb2 = SystemCoreClock;
    USART1->BRR = (f_apb2 + (baud / 2U)) / baud;

    // --- TX DMA: DMA2 Stream7, channel 4, memory -> peripheral ------------
    DMA2_Stream7->CR &= ~DMA_SxCR_EN;
    while ((DMA2_Stream7->CR & DMA_SxCR_EN) != 0U)
      {
      }
    DMA2->HIFCR = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7
        | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;
    DMA2_Stream7->PAR = reinterpret_cast<std::uint32_t> (&USART1->DR);
    DMA2_Stream7->M0AR = reinterpret_cast<std::uint32_t> (g_tx_buf);
    DMA2_Stream7->NDTR = 0U;
    DMA2_Stream7->CR = (4U << DMA_SxCR_CHSEL_Pos) // channel 4 (USART1_TX)
        | DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_PL_1 | DMA_SxCR_TCIE;
    USART1->CR3 |= USART_CR3_DMAT;

    // Enable transmitter, receiver and the USART itself (8N1, no parity).
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    // Clear any pending flag before enabling the RX interrupt.
    (void) USART1->SR;
    (void) USART1->DR;
    USART1->CR1 |= USART_CR1_RXNEIE;

    irq_enable (USART1_IRQn);
    irq_enable (DMA2_Stream7_IRQn);
  }

  void
  put_char (char c)
  {
    (void) g_tx_free.wait ();

    __disable_irq ();
    g_tx_buf[g_tx_head] = static_cast<std::uint8_t> (c);
    g_tx_head = ring_next (g_tx_head, TX_SIZE);
    tx_start ();
    __enable_irq ();
  }

  void
  write (const char* s)
  {
    for (; *s != '\0'; ++s)
      {
        if (*s == '\n')
          {
            put_char ('\r');
          }
        put_char (*s);
      }
  }

  void
  write (const char* s, std::size_t n)
  {
    for (std::size_t i = 0; i < n; ++i)
      {
        if (s[i] == '\n')
          {
            put_char ('\r');
          }
        put_char (s[i]);
      }
  }

  int
  read_char (void)
  {
    (void) g_rx_ready.wait ();
    return rx_pop ();
  }

  int
  timed_read_char (std::uint32_t timeout_ms)
  {
    if (g_rx_ready.timed_wait (timeout_ms) != result::ok)
      {
        return -1;
      }
    return rx_pop ();
  }

  int
  try_read_char (void)
  {
    if (g_rx_ready.try_wait () != result::ok)
      {
        return -1;
      }
    return rx_pop ();
  }

  std::size_t
  rx_available (void)
  {
    __disable_irq ();
    const std::size_t head = g_rx_head;
    const std::size_t tail = g_rx_tail;
    __enable_irq ();
    return (head >= tail) ? (head - tail) : (RX_SIZE - tail + head);
  }
}

// ---- device interrupt handlers -------------------------------------------

extern "C" void
USART1_IRQHandler (void)
{
  if ((USART1->SR & USART_SR_RXNE) != 0U)
    {
      const std::uint8_t b = static_cast<std::uint8_t> (USART1->DR);
      const std::size_t next = ring_next (g_rx_head, RX_SIZE);
      if (next != g_rx_tail)
        {
          g_rx_buf[g_rx_head] = b;
          g_rx_head = next;
          (void) g_rx_ready.post ();
        }
      // else: ring full, drop the byte
    }
}

extern "C" void
DMA2_Stream7_IRQHandler (void)
{
  if ((DMA2->HISR & DMA_HISR_TCIF7) != 0U)
    {
      DMA2->HIFCR = DMA_HIFCR_CTCIF7;

      const std::size_t len = g_tx_chunk;
      std::size_t tail = g_tx_tail + len;
      if (tail >= TX_SIZE)
        {
          tail -= TX_SIZE;
        }
      g_tx_tail = tail;
      g_tx_active = false;

      for (std::size_t i = 0; i < len; ++i)
        {
          (void) g_tx_free.post ();
        }

      __disable_irq ();
      tx_start ();
      __enable_irq ();
    }
}
