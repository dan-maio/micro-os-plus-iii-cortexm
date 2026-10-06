/*
 * pipeline.hpp — the shape of the run: sizes, the two hand-off slots, the
 * eight semaphores, and the per-iteration verdict record.
 *
 * Everything here is global, because all of it is shared between five
 * threads created and joined eight times over. Names follow the Rust
 * reference (docs/Rust/weact-spi.md).
 */

#ifndef SPI_PIPELINE_PIPELINE_HPP_
#define SPI_PIPELINE_PIPELINE_HPP_

#include <cstdint>
#include <cstddef>

#include <cmsis-plus/rtos/os.h>

namespace pipeline
{
  // ---- sizes ----------------------------------------------------------

  // Bytes Thread 1 moves per hand-off.
  inline constexpr std::size_t CHUNK = 256;
  // The widest encoded chunk -> the encoded slot's size.
  inline constexpr std::size_t B64_MAX = 344; // base64::encoded_len(256)

  inline std::size_t
  chunks (std::size_t len)
  {
    return (len + CHUNK - 1) / CHUNK;
  }

  inline std::size_t
  chunk_len (std::size_t len, std::size_t i)
  {
    const std::size_t start = i * CHUNK;
    if (start >= len)
      {
        return 0;
      }
    if (len - start < CHUNK)
      {
        return len - start;
      }
    return CHUNK;
  }

  inline constexpr std::size_t ITERATIONS = 8;
  inline constexpr std::size_t BLINK_CYCLES = 5;
  inline constexpr std::uint32_t BLINK_HALF = 300;   // ms (1 tick == 1 ms)
  inline constexpr std::uint32_t PRODUCE_SPAN = 2400; // ms for the whole source
  inline constexpr std::uint32_t SNAPSHOT_WAIT = 400; // ms
  inline constexpr std::size_t SNAPSHOT_SHOW = 96;
  inline constexpr std::size_t STACK = 4096;

  // Per-chunk delay so the whole source takes PRODUCE_SPAN.
  inline std::uint32_t
  pace (std::size_t len)
  {
    const std::size_t c = chunks (len);
    std::uint32_t step = c ? (PRODUCE_SPAN / static_cast<std::uint32_t> (c)) : 0;
    if (step < 1)
      {
        step = 1;
      }
    return step;
  }

  inline std::uint32_t
  encoded_total (std::size_t len)
  {
    const std::size_t full = len / CHUNK;
    const std::size_t rem = len % CHUNK;
    // encoded_len(n) = (n + 2)/3 * 4
    auto enc = [] (std::size_t n) { return ((n + 2) / 3) * 4; };
    return static_cast<std::uint32_t> (full * enc (CHUNK)
                                       + (rem ? enc (rem) : 0));
  }

  // ---- the two hand-off slots ------------------------------------------

  struct Slot
  {
    std::uint8_t data[CHUNK > B64_MAX ? CHUNK : B64_MAX];
    std::size_t len;
    bool last;
  };

  // The raw buffer: Thread 1 fills it, Thread 2 empties it.
  extern Slot g_raw;
  // The encoded buffer: Thread 2 fills it, Thread 3 empties it.
  extern Slot g_enc;

  // ---- the eight semaphores -------------------------------------------

  extern os::rtos::semaphore_counting g_buf_empty;
  extern os::rtos::semaphore_counting g_buf_full;
  extern os::rtos::semaphore_counting g_b64_empty;
  extern os::rtos::semaphore_counting g_b64_full;
  extern os::rtos::semaphore_counting g_copy_done;
  extern os::rtos::semaphore_counting g_file_done;
  extern os::rtos::semaphore_counting g_verify_done;
  extern os::rtos::semaphore_counting g_write_snapshot;

  // ---- the verdict record ---------------------------------------------

  struct Run
  {
    std::uint32_t crc0;
    std::uint32_t crc1;
    std::size_t chunks;
    std::uint32_t enc_bytes;
    std::uint32_t dec_bytes;
    bool final_seen;
    std::size_t cycles;
    std::size_t fresh;
    std::uint32_t longest;
    bool monotonic;
    std::uint32_t errors;
  };

  extern Run g_run;

  // The spec's reset_pipeline_state(): put the semaphores back exactly where
  // an iteration expects them (some are drained, the two "empty" ones refilled
  // to exactly one token), and clear the slots and the verdict record.
  void
  reset ();

  // Note that something went wrong, from whichever thread noticed.
  void
  fault (const char* what);

  // ---- the console ----------------------------------------------------

  // Guards access to USART1. Reentrant: the banner holds it while each line
  // takes it again.
  extern os::rtos::mutex_recursive g_console;

  // Emit one NUL-terminated line under the console lock.
  void
  line (const char* s);

  // Emit raw bytes under the console lock (no newline translation).
  void
  raw (const std::uint8_t* data, std::size_t len);
}

#endif /* SPI_PIPELINE_PIPELINE_HPP_ */
