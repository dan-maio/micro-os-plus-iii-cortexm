/*
 * psram.cpp - bare-metal RP2350 QSPI PSRAM driver (port of the pico-sdk
 * PSRAM examples: psram_tool.c / sfe_psram.c).
 *
 * Placed in .psram_ram so boot.S can copy it to internal SRAM and run it
 * there: while the QMI is in direct mode (asserting CS1 for the PSRAM
 * commands), it cannot service the flash XIP that the code itself would be
 * fetched from.
 */

#include <bsp/psram.hpp>

#include <cstdint>

namespace
{
  // MUST be always-inlined: reg()/wait_until() are called from inside the QMI
  // direct-mode window, where flash XIP is disabled. An out-of-line copy in
  // flash would stall the bus forever when fetched (this hung read_id()).
  __attribute__ ((always_inline)) inline volatile std::uint32_t&
  reg (std::uint32_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  // RP2350 register bases.
  constexpr std::uint32_t QMI_BASE      = 0x400d0000U;
  constexpr std::uint32_t XIP_CTRL      = 0x400c8000U;
  constexpr std::uint32_t IO_BANK0_BASE = 0x40028000U;
  constexpr std::uint32_t PADS_BANK0    = 0x40038000U;

  // QMI register offsets.
  constexpr std::uint32_t QMI_DIRECT_CSR = QMI_BASE + 0x00U;
  constexpr std::uint32_t QMI_DIRECT_TX  = QMI_BASE + 0x04U;
  constexpr std::uint32_t QMI_DIRECT_RX  = QMI_BASE + 0x08U;
  constexpr std::uint32_t QMI_M1_TIMING  = QMI_BASE + 0x20U;
  constexpr std::uint32_t QMI_M1_RFMT    = QMI_BASE + 0x24U;
  constexpr std::uint32_t QMI_M1_RCMD    = QMI_BASE + 0x28U;
  constexpr std::uint32_t QMI_M1_WFMT    = QMI_BASE + 0x2cU;
  constexpr std::uint32_t QMI_M1_WCMD    = QMI_BASE + 0x30U;

  // QMI DIRECT_CSR bits.
  constexpr std::uint32_t D_EN      = 0x00000001U;
  constexpr std::uint32_t D_BUSY    = 0x00000002U;
  constexpr std::uint32_t D_CS1N    = 0x00000008U;
  constexpr std::uint32_t D_TXEMPTY = 0x00000800U;
  constexpr std::uint32_t D_CLKDIV  = 30u << 22; // ~5 MHz @ 150 MHz

  // GPIO / pad.
  constexpr std::uint32_t GPIO_FUNC_XIP_CS1 = 9U;
  constexpr std::uint32_t PADS_ISO = 1u << 8;
  constexpr std::uint32_t PADS_IE  = 1u << 6;

  // PSRAM chip-select pin (XIP CS1 function). Board-selectable at build time:
  // the WeAct RP2350B default routes it to GPIO0; the Pi-Zero-form board wires
  // the QSPI PSRAM CS to GPIO47 (-DPICO2_PSRAM_CS_PIN=47).
#if !defined(PICO2_PSRAM_CS_PIN)
#define PICO2_PSRAM_CS_PIN 0
#endif
  constexpr std::uint32_t CS_PIN = PICO2_PSRAM_CS_PIN;
  // CTRL for GPIO n at IO_BANK0 + n*8 + 4; PAD reg at PADS + 0x04 + n*4.
  constexpr std::uint32_t CS_GPIO_CTRL = IO_BANK0_BASE + CS_PIN * 8 + 4;
  constexpr std::uint32_t CS_PAD_CTRL  = PADS_BANK0 + 0x04 + CS_PIN * 4;

  // PSRAM command codes.
  constexpr std::uint8_t CMD_RSTEN      = 0x66;
  constexpr std::uint8_t CMD_RST        = 0x99;
  constexpr std::uint8_t CMD_QUAD_EN    = 0x35;
  constexpr std::uint8_t CMD_QUAD_READ  = 0xEB;
  constexpr std::uint8_t CMD_QUAD_WRITE = 0x38;

  // Bounded wait: returns true if the condition cleared in time. Always inline
  // (runs inside the direct-mode window; see reg() above).
  __attribute__ ((always_inline)) inline bool
  wait_until (volatile std::uint32_t& csr, std::uint32_t mask, bool set)
  {
    for (std::uint32_t n = 0; n < 100000U; ++n)
      {
        bool now = (csr & mask) != 0;
        if (now == set)
          {
            return true;
          }
      }
    return false;
  }
} // namespace

namespace psram
{
  bool
  init (void) __attribute__ ((section (".psram_ram"), noinline));

  bool
  init (void)
  {
    // Disable IRQs for the whole sequence: any interrupt fired during QMI
    // direct mode would fetch its handler from flash XIP, which the QMI
    // cannot service while busy with CS1 — that access stalls forever.
    std::uint32_t primask;
    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i");
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

    // 1. Route the CS pin to the XIP CS1 function and un-isolate the pad.
    reg (CS_GPIO_CTRL) = (reg (CS_GPIO_CTRL) & ~0x1FU) | GPIO_FUNC_XIP_CS1;
    reg (CS_PAD_CTRL) = (reg (CS_PAD_CTRL) & ~PADS_ISO) | PADS_IE;

    // 2. QMI direct mode (5 MHz), wait for the last XIP transfer cooldown.
    reg (QMI_DIRECT_CSR) = D_CLKDIV | D_EN;
    wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);

    // 3. Reset the PSRAM and enable quad-SPI mode (CS1 toggled per byte).
    //    No quad-exit (0xF5) frame: on a fresh boot the PSRAM is in SPI mode,
    //    and RSTEN/RST below resets it to SPI regardless of any previous quad
    //    state. The quad-exit frame left a phantom byte stuck in the
    //    DIRECT_RX FIFO that no read could pop, which wedged the interface.
    for (std::uint32_t i = 0; i < 34; ++i)
      {
        reg (QMI_DIRECT_CSR) |= D_CS1N;
        std::uint32_t cmd = (i == 0) ? CMD_RSTEN : (i == 1) ? CMD_RST : CMD_QUAD_EN;
        reg (QMI_DIRECT_TX) = cmd;
        wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);
        reg (QMI_DIRECT_CSR) &= ~D_CS1N;
        for (volatile std::uint32_t j = 0; j < 20; ++j)
          {
          }
        (void) reg (QMI_DIRECT_RX);
      }

    // 4. Leave direct mode.
    reg (QMI_DIRECT_CSR) &= ~(D_CS1N | D_EN);

    // 5. M1 timing: 150 MHz / 2 = 75 MHz PSRAM SCK, generous CS times.
    reg (QMI_M1_TIMING)
        = (0x2u << 28)   // PAGEBREAK at 1 KB
          | (3u << 23)   // SELECT_HOLD = 3 extra sys cycles
          | (1u << 30)   // COOLDOWN = 1
          | (1u << 8)    // RXDELAY = 1
          | (14u << 17)  // MAX_SELECT ~8 us
          | (6u << 12)   // MIN_DESELECT ~50 ns
          | (2u << 0);   // CLKDIV = 2

    // 6. M1 read: 0xEB quad read, prefix 8, suffix 0, dummy 24, all quad.
    reg (QMI_M1_RFMT)
        = (0x2u << 0) | (0x2u << 2) | (0x2u << 4) | (0x2u << 6) | (0x2u << 8)
          | (0x1u << 12) | (0x0u << 14) | (0x6u << 16);
    reg (QMI_M1_RCMD) = CMD_QUAD_READ;

    // 7. M1 write: 0x38 quad write, prefix 8, no suffix, no dummy.
    reg (QMI_M1_WFMT)
        = (0x2u << 0) | (0x2u << 2) | (0x2u << 4) | (0x2u << 6) | (0x2u << 8)
          | (0x1u << 12) | (0x0u << 14) | (0x0u << 16);
    reg (QMI_M1_WCMD) = CMD_QUAD_WRITE;

    // 8. Allow writes to the PSRAM window (XIP_CTRL bit 11 = WRITABLE_M1).
    reg (XIP_CTRL) |= 0x800u;

    // 9. Sanity check: write + read back a word in window 1.
    volatile std::uint32_t* p = reinterpret_cast<volatile std::uint32_t*> (0x11000000U);
    *p = 0xA5A5A5A5U;
    __asm__ volatile ("dmb sy" ::: "memory");
    bool ok = (*p == 0xA5A5A5A5U);

    // 10. Restore IRQ state.
    __asm__ volatile ("msr primask, %0" ::"r" (primask));
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");
    return ok;
  }

  // Read the PSRAM device ID (SPI command 0x9F) via QMI direct mode. Must run
  // from SRAM (.psram_ram) like init(). Fills out[0..7]:
  //   out[0] = MFID (APS6404 = 0x0D), out[1] = KGD (0x5D), out[2..7] = EID.
  // Call on a fresh PSRAM (SPI mode); it issues RSTEN/RST first to be safe.
  void
  read_id (std::uint8_t out[8]) __attribute__ ((section (".psram_ram"), noinline));

  void
  read_id (std::uint8_t out[8])
  {
    std::uint32_t primask;
    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i");
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

    // Route CS pin to XIP CS1 + un-isolate pad (same as init step 1).
    reg (CS_GPIO_CTRL) = (reg (CS_GPIO_CTRL) & ~0x1FU) | GPIO_FUNC_XIP_CS1;
    reg (CS_PAD_CTRL) = (reg (CS_PAD_CTRL) & ~PADS_ISO) | PADS_IE;

    // Direct mode, slow clock.
    reg (QMI_DIRECT_CSR) = D_CLKDIV | D_EN;
    wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);

    // RSTEN then RST (each its own CS frame) to force SPI mode.
    for (std::uint32_t i = 0; i < 2; ++i)
      {
        reg (QMI_DIRECT_CSR) |= D_CS1N;
        reg (QMI_DIRECT_TX) = (i == 0) ? CMD_RSTEN : CMD_RST;
        wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);
        reg (QMI_DIRECT_CSR) &= ~D_CS1N;
        for (volatile std::uint32_t j = 0; j < 200; ++j)
          {
          }
        (void) reg (QMI_DIRECT_RX);
      }

    // Read ID frame: hold CS1 for command 0x9F + 3 address bytes + 8 read.
    reg (QMI_DIRECT_CSR) |= D_CS1N;
    const std::uint8_t hdr[4] = { 0x9F, 0x00, 0x00, 0x00 };
    for (std::uint32_t i = 0; i < 4; ++i)
      {
        reg (QMI_DIRECT_TX) = hdr[i];
        wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);
        (void) reg (QMI_DIRECT_RX);
      }
    for (std::uint32_t i = 0; i < 8; ++i)
      {
        reg (QMI_DIRECT_TX) = 0x00;
        wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);
        out[i] = static_cast<std::uint8_t> (reg (QMI_DIRECT_RX) & 0xFFU);
      }
    reg (QMI_DIRECT_CSR) &= ~(D_CS1N | D_EN);

    __asm__ volatile ("msr primask, %0" ::"r" (primask));
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");
  }
} // namespace psram
