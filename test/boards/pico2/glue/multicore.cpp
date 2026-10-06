/*
 * multicore.cpp — RP2350 dual Cortex-M33 launch + SIO FIFO.
 * Faithful C++ port of reference/src/multicore.c.
 */

#include <bsp/multicore.hpp>
#include <bsp/rp2350.hpp>

using rp2350::reg;

// The shared ARM vector table (interrupt-vectors.S).
extern "C" std::uint32_t __vector_table[];

namespace
{
  constexpr std::uint32_t PSM_FRCE_OFF = rp2350::PSM_BASE + 0x04;
  constexpr std::uint32_t PSM_FRCE_OFF_PROC1 = 1U << 24;

  void
  sev (void)
  {
    __asm__ volatile ("sev");
  }

  // Force a volatile read of the RX FIFO (pops one word) and discard it.
  // NB: `(void) reg(SIO_FIFO_RD)` does NOT do this in C++ — casting a volatile
  // lvalue to void does not force the lvalue-to-rvalue conversion, so the
  // register is never actually read and the FIFO is not popped.
  void
  discard_rd (void)
  {
    volatile std::uint32_t sink = reg (rp2350::SIO_FIFO_RD);
    (void) sink;
  }

  void
  fifo_drain (void)
  {
    while (reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_VLD)
      {
        discard_rd ();
      }
  }
}

namespace multicore
{
  bool
  fifo_write_ready (void)
  {
    return (reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_RDY) != 0;
  }

  bool
  fifo_read_valid (void)
  {
    return (reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_VLD) != 0;
  }

  void
  fifo_push (std::uint32_t v)
  {
    while (!fifo_write_ready ())
      {
      }
    reg (rp2350::SIO_FIFO_WR) = v;
    sev ();
  }

  std::uint32_t
  fifo_pop (void)
  {
    while (!fifo_read_valid ())
      {
      }
    return reg (rp2350::SIO_FIFO_RD);
  }

  void
  launch_core1 (void (*entry) (void), std::uint32_t stack_top)
  {
    const std::uint32_t cmd_seq[6]
        = { 0, 0, 1, reinterpret_cast<std::uint32_t> (__vector_table),
            stack_top, reinterpret_cast<std::uint32_t> (entry) };

    // Force core 1 into reset, then release it into the bootrom launch loop
    // (exactly the pico-sdk multicore_reset_core1(): set FRCE_OFF.PROC1, read
    // back, clear). The caller MUST have disabled the SIO FIFO IRQ: the
    // bootrom uses the same FIFO for this handshake.
    reg (PSM_FRCE_OFF) |= PSM_FRCE_OFF_PROC1;
    while (!(reg (PSM_FRCE_OFF) & PSM_FRCE_OFF_PROC1))
      {
      }
    reg (PSM_FRCE_OFF) &= ~PSM_FRCE_OFF_PROC1;

    // Drain any stale words, then wait for core 1's bootrom readiness 0. The
    // bootrom FIRST drains its own mailbox FIFO, THEN pushes the 0 — so
    // consuming it proves core 1 is past the drain and waiting for commands.
    // Sending any command before that point lets core 1's drain DISCARD it,
    // the echo never comes, and the handshake livelocks (the pico-sdk blocks
    // here with multicore_fifo_pop_blocking()). The guard only exists so a
    // pathological case cannot hang forever. NB: when the boot flow already
    // booted core 1 into the launch loop (its readiness was pushed before our
    // firmware ran and is drained above), the readiness never re-arrives here
    // and the seq loop below handles the handshake directly — a few ms is
    // ample to distinguish that case from a slow post-reset boot.
    fifo_drain ();
    {
      volatile std::uint32_t guard = 0;
      while (!fifo_read_valid ())
        {
          if (++guard > 2000000U)
            {
              break;
            }
        }
      if (fifo_read_valid ())
        {
          discard_rd (); // consume the readiness 0
        }
    }

    // Send {0,0,1,vtor,sp,entry}; core 1 echoes each word, advance on match,
    // restart on mismatch (exactly the pico-sdk / reference handshake).
    unsigned seq = 0;
    do
      {
        std::uint32_t cmd = cmd_seq[seq];
        if (cmd == 0)
          {
            fifo_drain ();
            sev ();
          }
        fifo_push (cmd);
        std::uint32_t response = fifo_pop ();
        seq = (cmd == response) ? seq + 1 : 0;
      }
    while (seq < 6);
  }
}
