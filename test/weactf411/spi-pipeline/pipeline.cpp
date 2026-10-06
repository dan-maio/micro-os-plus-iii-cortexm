/*
 * pipeline.cpp — the shared state of the spi-pipeline example. See
 * pipeline.hpp for the roles.
 */

#include <pipeline.hpp>

#include <cstring>

#include <uart.hpp>

using namespace os::rtos;

namespace pipeline
{
  Slot g_raw;
  Slot g_enc;

  // Counting semaphores of max 1: the four "signal" ones start empty, the two
  // "empty" ones start with one token (the spec's fix 7 — though reset()
  // owns that invariant, see pipeline.hpp).
  semaphore_counting g_buf_empty{ "buf_empty", 1, 1 };
  semaphore_counting g_buf_full{ "buf_full", 1, 0 };
  semaphore_counting g_b64_empty{ "b64_empty", 1, 1 };
  semaphore_counting g_b64_full{ "b64_full", 1, 0 };
  semaphore_counting g_copy_done{ "copy_done", 1, 0 };
  semaphore_counting g_file_done{ "file_done", 1, 0 };
  semaphore_counting g_verify_done{ "verify_done", 1, 0 };
  semaphore_counting g_write_snapshot{ "write_snapshot", 1, 0 };

  Run g_run{};

  mutex_recursive g_console{ "console" };

  void
  drain (semaphore_counting& s)
  {
    while (s.try_wait () == result::ok)
      {
        ;
      }
  }

  void
  refill (semaphore_counting& s)
  {
    drain (s);
    (void) s.post ();
  }

  void
  reset ()
  {
    refill (g_buf_empty);
    refill (g_b64_empty);
    drain (g_buf_full);
    drain (g_b64_full);
    drain (g_copy_done);
    drain (g_file_done);
    drain (g_verify_done);
    drain (g_write_snapshot);

    const std::uint32_t crc0 = g_run.crc0;
    std::memset (&g_run, 0, sizeof (g_run));
    g_run.crc0 = crc0;
    g_run.monotonic = true; // Run::new() in the reference starts it true

    g_raw.len = 0;
    g_raw.last = false;
    g_enc.len = 0;
    g_enc.last = false;
  }

  void
  fault (const char* what)
  {
    char line_buf[96];
    int n = snprintf (line_buf, sizeof (line_buf),
                      "spi-pipeline: fault: %s\n", what);
    if (n > 0)
      {
        line (line_buf);
      }
    g_run.errors++;
  }

  void
  line (const char* s)
  {
    g_console.lock ();
    uart::write (s);
    g_console.unlock ();
  }

  void
  raw (const std::uint8_t* data, std::size_t len)
  {
    g_console.lock ();
    for (std::size_t i = 0; i < len; ++i)
      {
        uart::put_char (static_cast<char> (data[i]));
      }
    g_console.unlock ();
  }
}
