/*
 * multicore.hpp — RP2040 dual Cortex-M0+ launch + SIO FIFO primitives.
 */
#ifndef PICO_BSP_MULTICORE_HPP_
#define PICO_BSP_MULTICORE_HPP_

#include <cstdint>

namespace multicore
{
  // Launch core 1 at `entry` with the given top-of-stack, via the bootrom
  // FIFO handshake {0,0,1, vtor, sp, entry}.
  void
  launch_core1 (void (*entry) (void), std::uint32_t stack_top);

  // SIO inter-core FIFO (used later as the SMP IPI channel).
  bool
  fifo_write_ready (void);
  bool
  fifo_read_valid (void);
  void
  fifo_push (std::uint32_t v);
  std::uint32_t
  fifo_pop (void);
}

#endif /* PICO_BSP_MULTICORE_HPP_ */
