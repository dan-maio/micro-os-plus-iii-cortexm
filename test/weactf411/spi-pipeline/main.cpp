/*
 * spi-pipeline — the SPI filesystem pipeline on the WeAct F411CE, in C++ on
 * the shared micro-os-plus-iii kernel.
 *
 * The application specified by docs/Rust/weact-spi.md, ported from the Rust
 * example (examples/spi-pipeline in the Rust board crate) to the C++ µOS++
 * kernel. Five threads, two buffers, eight semaphores, two partitions, a CRC
 * that has to come back the same — ITERATIONS times.
 *
 *   image ──T1──▶ [buffer] ──T2──▶ [buffer_b64] ──T3──▶ enc<n>.b64
 *    0x08000000    256 B    Base64    ≤344 B    append   (partition 1)
 *                                                  │
 *                                       snapshot   ├──▶ T4  blink + UART
 *                                       file_done  └──▶ T5  decode, CRC,
 *                                                            dec<n>.bin
 *                                                            (partition 2)
 *
 * Every check is made in-program and printed as a verdict; the last line is
 * PASS or FAIL, n. Semihosting carries the CRC triplet; the console is USART1.
 */

#include <cmsis-plus/rtos/os.h>
#include <cmsis-plus/diag/trace.h>

#include <board.hpp>
#include <led.hpp>
#include <uart.hpp>
#include <spi.hpp>
#include <flatfs.hpp>
#include <hw_result.hpp>

#include <base64.hpp>
#include <pipeline.hpp>
#include <store.hpp>

#include <cstdio>
#include <cstring>
#include <cstdarg>

using namespace os::rtos;

// Where the application image is mapped.
inline constexpr std::uint32_t IMAGE_BASE = 0x0800'0000U;

extern "C" std::uint32_t _sidata;
extern "C" std::uint32_t _sdata;
extern "C" std::uint32_t _edata;

namespace
{
  // The application image length: every byte the linker put in flash, up to
  // the end of .data's flash copy (the last thing in the image).
  std::size_t
  image_len ()
  {
    const std::uint32_t data_len = (&_edata - &_sdata);
    const std::uint32_t end = reinterpret_cast<std::uint32_t> (&_sidata)
                              + data_len;
    return static_cast<std::size_t> (end - IMAGE_BASE);
  }

  const std::uint8_t*
  image ()
  {
    return reinterpret_cast<const std::uint8_t*> (IMAGE_BASE);
  }

  std::size_t
  heap_free_bytes ()
  {
    return os::rtos::memory::get_default_resource ()->free_bytes ();
  }

  // ---- a scoped lock on the device ----------------------------------------

  class device_lock
  {
  public:
    device_lock ()
    {
      store::mutex ().lock ();
    }
    ~device_lock ()
    {
      store::mutex ().unlock ();
    }
    store::Storage*
    operator-> ()
    {
      return &store::instance ();
    }
  };

  // ---- one formatted console line under the console lock ------------------

  void
  linef (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));

  void
  linef (const char* fmt, ...)
  {
    char buf[192];
    va_list ap;
    va_start (ap, fmt);
    vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    pipeline::line (buf);
  }

  // ---- per-thread arguments ----------------------------------------------

  struct t3_args
  {
    char enc_name[9];
    std::size_t src_len;
  };

  struct t4_args
  {
    std::size_t iter;
    char enc_name[9];
  };

  struct t5_args
  {
    char enc_name[9];
    char dec_name[9];
    std::size_t src_len;
  };
}

// ---- Thread 1: image -> the raw slot -------------------------------------

void*
t1_image_to_buffer (void* arg)
{
  const std::size_t len = reinterpret_cast<std::size_t> (arg);
  const std::uint32_t step = pipeline::pace (len);
  std::size_t offset = 0;

  while (offset < len)
    {
      if (pipeline::g_buf_empty.wait () != result::ok)
        {
          pipeline::fault ("T1 buf_empty");
          return nullptr;
        }

      const std::size_t n = (len - offset) < pipeline::CHUNK
                                ? (len - offset)
                                : pipeline::CHUNK;
      std::memcpy (pipeline::g_raw.data, image () + offset, n);
      pipeline::g_raw.len = n;
      pipeline::g_raw.last = (offset + n == len);
      offset += n;

      (void) pipeline::g_buf_full.post ();

      sysclock.sleep_for (step);
    }

  (void) pipeline::g_copy_done.post ();
  return nullptr;
}

// ---- Thread 2: Base64 ----------------------------------------------------

void*
t2_base64_encode (void* /*arg*/)
{
  for (;;)
    {
      if (pipeline::g_b64_empty.wait () != result::ok)
        {
          pipeline::fault ("T2 b64_empty");
          return nullptr;
        }
      if (pipeline::g_buf_full.wait () != result::ok)
        {
          pipeline::fault ("T2 buf_full");
          return nullptr;
        }

      const bool last = [&] () {
        pipeline::g_enc.len = base64::encode (pipeline::g_raw.data,
                                              pipeline::g_raw.len,
                                              pipeline::g_enc.data);
        pipeline::g_enc.last = pipeline::g_raw.last;
        pipeline::g_raw.len = 0;
        return pipeline::g_enc.last;
      } ();

      (void) pipeline::g_buf_empty.post ();
      (void) pipeline::g_b64_full.post ();

      if (last)
        {
          return nullptr;
        }
    }
}

// ---- Thread 3: append to the encoded file --------------------------------

void*
t3_write_spi (void* arg)
{
  const t3_args* a = static_cast<const t3_args*> (arg);
  const char* name = a->enc_name;
  const std::size_t name_len = std::strlen (name);
  const std::size_t len = a->src_len;

  bool writing = true;
  bool created = false;

  {
    device_lock spi;
    if (spi->create (store::Which::encoded, name, name_len) != 0)
      {
        pipeline::fault ("T3 create");
        writing = false;
      }
    else
      {
        created = true;
      }
  }

  std::size_t written = 0;
  bool final_seen = false;

  for (;;)
    {
      if (pipeline::g_b64_full.wait () != result::ok)
        {
          pipeline::fault ("T3 b64_full");
          break;
        }

      const bool last = [&] () {
        if (writing)
          {
            device_lock spi;
            if (spi->write (pipeline::g_enc.data, pipeline::g_enc.len) != 0)
              {
                pipeline::fault ("T3 write");
                writing = false;
              }
            else
              {
                ++written;
              }
          }
        return pipeline::g_enc.last;
      } ();

      (void) pipeline::g_b64_empty.post ();
      // Binary: an overflow here means Thread 4 has not taken the previous
      // token yet, which is not news. Deliberately discarded.
      (void) pipeline::g_write_snapshot.post ();

      if (last)
        {
          final_seen = true;
        }
      if (final_seen || written >= pipeline::chunks (len))
        {
          break;
        }
    }

  // The file is not closed until the producer has confirmed the source is
  // exhausted (spec defect 13).
  (void) pipeline::g_copy_done.wait ();

  std::uint32_t enc_bytes = 0;
  if (created)
    {
      device_lock spi;
      if (writing)
        {
          enc_bytes = spi->open_len ();
        }
      // Always close a created file, even after a failed write.
      if (spi->close () != 0)
        {
          pipeline::fault ("T3 close");
        }
    }

  pipeline::g_run.chunks = written;
  pipeline::g_run.enc_bytes = enc_bytes;
  pipeline::g_run.final_seen = final_seen;

  (void) pipeline::g_file_done.post ();
  return nullptr;
}

// ---- Thread 4: blink, and watch the file grow -----------------------------

namespace
{
  void
  show (const std::uint8_t* head, std::size_t hn, const std::uint8_t* tail,
        std::size_t tn, std::uint32_t len)
  {
    if (hn == 0)
      {
        linef ("           (empty)\n");
        return;
      }
    linef ("           ");
    pipeline::raw (head, hn);
    if (tn != 0)
      {
        linef (" ... %u more ... ",
               static_cast<unsigned> (len - hn - tn));
        pipeline::raw (tail, tn);
      }
    linef ("\n");
  }
}

void*
t4_blink_uart (void* arg)
{
  const t4_args* a = static_cast<const t4_args*> (arg);
  const std::size_t iter = a->iter;
  const char* name = a->enc_name;
  const std::size_t name_len = std::strlen (name);

  std::uint8_t head[pipeline::SNAPSHOT_SHOW];
  std::uint8_t tail[pipeline::SNAPSHOT_SHOW];
  std::uint32_t previous = 0;
  std::size_t cycles = 0;
  std::size_t fresh = 0;
  bool monotonic = true;

  for (std::size_t cycle = 0; cycle < pipeline::BLINK_CYCLES; ++cycle)
    {
      led::toggle ();
      sysclock.sleep_for (pipeline::BLINK_HALF);
      led::toggle ();
      sysclock.sleep_for (pipeline::BLINK_HALF);

      // Bounded wait: a timeout means "nothing new since last time", which is
      // the ordinary case once the writer has finished.
      const bool new_chunk
          = (pipeline::g_write_snapshot.timed_wait (pipeline::SNAPSHOT_WAIT)
             == result::ok);

      std::uint32_t len;
      std::size_t hn, tn;
      {
        device_lock spi;
        len = spi->visible_len (store::Which::encoded, name, name_len);
        hn = spi->read_visible (store::Which::encoded, name, name_len, 0, head,
                                sizeof (head));
        const std::uint32_t at
            = len > pipeline::SNAPSHOT_SHOW ? len - pipeline::SNAPSHOT_SHOW : 0;
        tn = (at > hn) ? spi->read_visible (store::Which::encoded, name,
                                            name_len, at, tail, sizeof (tail))
                       : 0;
      }

      if (len < previous)
        {
          monotonic = false;
        }
      previous = previous > len ? previous : len;
      cycles++;
      if (new_chunk)
        {
          fresh++;
        }

      linef ("[%8lu ms] iter %lu cycle %lu: %u bytes on the file%s\n",
             static_cast<unsigned long> (sysclock.now ()),
             static_cast<unsigned long> (iter),
             static_cast<unsigned long> (cycle),
             static_cast<unsigned> (len), new_chunk ? ", new chunk" : "");
      show (head, hn, tail, tn, len);
    }

  led::off ();

  pipeline::g_run.cycles = cycles;
  pipeline::g_run.fresh = fresh;
  pipeline::g_run.longest = previous;
  pipeline::g_run.monotonic = pipeline::g_run.monotonic && monotonic;
  return nullptr;
}

// ---- Thread 5: read back, decode, verify ---------------------------------

void*
t5_decode_verify (void* arg)
{
  const t5_args* a = static_cast<const t5_args*> (arg);
  const char* enc_name = a->enc_name;
  const std::size_t enc_len = std::strlen (enc_name);
  const char* dec_name = a->dec_name;
  const std::size_t dec_len = std::strlen (dec_name);
  const std::size_t len = a->src_len;

  if (pipeline::g_file_done.wait () != result::ok)
    {
      pipeline::fault ("T5 file_done");
      (void) pipeline::g_verify_done.post ();
      return nullptr;
    }

  std::uint8_t piece[pipeline::B64_MAX];
  std::uint8_t plain[pipeline::CHUNK];
  std::uint32_t crc = flatfs::CRC32_INIT;
  std::uint32_t decoded = 0;
  bool ok = true;

  {
    device_lock spi;
    if (spi->create (store::Which::decoded, dec_name, dec_len) != 0)
      {
        pipeline::fault ("T5 create");
        ok = false;
      }
  }

  std::uint32_t at = 0;
  const std::size_t total = pipeline::chunks (len);
  for (std::size_t i = 0; i < total; ++i)
    {
      if (!ok)
        {
          break;
        }
      const std::size_t cl = pipeline::chunk_len (len, i);
      const std::size_t want = base64::encoded_len (cl);

      int got;
      {
        device_lock spi;
        got = spi->read_file (store::Which::encoded, enc_name, enc_len, at,
                              piece, want);
      }
      if (got < 0 || static_cast<std::size_t> (got) != want)
        {
          pipeline::fault ("T5 read");
          ok = false;
          break;
        }

      const std::size_t n
          = base64::decode (piece, want, plain, sizeof (plain));
      if (n == base64::NPOS)
        {
          pipeline::fault ("T5 chunk is not Base64");
          ok = false;
          break;
        }
      if (n != cl)
        {
          linef ("spi-pipeline: fault: T5 chunk %lu: %lu bytes, wanted %lu\n",
                 static_cast<unsigned long> (i),
                 static_cast<unsigned long> (n),
                 static_cast<unsigned long> (cl));
          pipeline::g_run.errors++;
          ok = false;
          break;
        }

      {
        device_lock spi;
        if (spi->write (plain, n) != 0)
          {
            pipeline::fault ("T5 write");
            ok = false;
            break;
          }
      }

      crc = flatfs::crc32_update (crc, plain, n);
      decoded += static_cast<std::uint32_t> (n);
      at += static_cast<std::uint32_t> (want);
    }

  {
    device_lock spi;
    // Close whatever the loop left: a short decoded file is still a file.
    if (spi->close () != 0 && ok)
      {
        pipeline::fault ("T5 close");
      }
  }

  const std::uint32_t crc1 = flatfs::crc32_final (crc);
  const std::uint32_t crc0 = pipeline::g_run.crc0;

  // Both channels: the gate reads the UART; the CRC triplet also goes to the
  // semihosting host.
  os::trace::printf ("CRC0 = 0x%08X\n", static_cast<unsigned> (crc0));
  os::trace::printf ("CRC1 = 0x%08X\n", static_cast<unsigned> (crc1));
  os::trace::printf ("%s\n", crc1 == crc0 ? "OK" : "!OK");

  linef ("[%8lu ms] verify: decoded %u bytes, CRC0 0x%08X CRC1 0x%08X %s\n",
         static_cast<unsigned long> (sysclock.now ()),
         static_cast<unsigned> (decoded), static_cast<unsigned> (crc0),
         static_cast<unsigned> (crc1), crc1 == crc0 ? "OK" : "!OK");

  pipeline::g_run.crc1 = crc1;
  pipeline::g_run.dec_bytes = decoded;

  (void) pipeline::g_verify_done.post ();
  return nullptr;
}

// ---- listing --------------------------------------------------------------

void
list_files ()
{
  for (store::Which which : { store::Which::encoded, store::Which::decoded })
    {
      device_lock spi;
      linef ("spi-pipeline: %s partition, %u bytes used\n",
             store::which_label (which),
             static_cast<unsigned> (spi->used (which)));
      spi->list (which, [] (const char* name, std::size_t name_len,
                            std::uint32_t size, std::uint32_t crc) {
        linef ("  %-16.*s %6u bytes  crc 0x%08x\n",
               static_cast<int> (name_len), name, static_cast<unsigned> (size),
               static_cast<unsigned> (crc));
      });
    }
}

// ---- the banner -----------------------------------------------------------

bool
banner (const char* backend, std::size_t src_len)
{
  // Holds the recursive console mutex while each line takes it again, proving
  // depth-2 re-entry.
  pipeline::g_console.lock ();
  linef ("=============================================\n");
  linef ("  WeAct-F411CE  spi-pipeline  store: %s\n", backend);
  linef ("  %lu B -> %lu chunks -> %u B -> %lu B\n",
         static_cast<unsigned long> (src_len),
         static_cast<unsigned long> (pipeline::chunks (src_len)),
         static_cast<unsigned> (pipeline::encoded_total (src_len)),
         static_cast<unsigned long> (src_len));
  linef ("=============================================\n");
  pipeline::g_console.unlock ();
  return true;
}

// ---- os_main --------------------------------------------------------------

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::init (board::UART_BAUD);
  spi::init ();

  linef ("\nspi-pipeline: heap %u bytes\n",
         static_cast<unsigned> (heap_free_bytes ()));

  // The codec is checked against the RFC 4648 vectors before anything else.
  const bool codec_ok = base64::self_test ();

  const std::size_t full = image_len ();

  const char* backend;
  std::size_t src_len;
  {
    device_lock spi;
    const spi::transport tr = spi::probe ();
    linef ("spi-pipeline: SPI transport %s (bulk from %u bytes)\n",
           spi::transport_name (tr), static_cast<unsigned> (spi::BULK_MIN));

    const bool on_flash = spi->probe ();
    store::Jedec j = spi->jedec ();
    linef ("spi-pipeline: JEDEC 0x%02x 0x%02x 0x%02x -> %s store\n",
           static_cast<unsigned> (j.manufacturer),
           static_cast<unsigned> (j.mem_type),
           static_cast<unsigned> (j.capacity), spi->backend ());
    src_len = on_flash ? full : (full < store::RAM_SOURCE_MAX
                                     ? full
                                     : store::RAM_SOURCE_MAX);
    spi->configure (src_len);

    const flatfs::Partition enc = spi->partition (store::Which::encoded);
    const flatfs::Partition dec = spi->partition (store::Which::decoded);
    // The semihosting witness of the layout, printed before the slow erase.
    os::trace::printf (
        "spi-pipeline: enc %u KiB @ %#010x, dec %u KiB @ %#010x (%u iterations)\n",
        static_cast<unsigned> (enc.size () / 1024),
        static_cast<unsigned> (enc.base ()),
        static_cast<unsigned> (dec.size () / 1024),
        static_cast<unsigned> (dec.base ()),
        static_cast<unsigned> (pipeline::ITERATIONS));

    if (spi->format () != 0)
      {
        linef ("spi-pipeline: FAIL, format\n");
        return 0;
      }
    linef ("spi-pipeline: enc %u KiB @ %#x, dec %u KiB @ %#x\n",
           static_cast<unsigned> (enc.size () / 1024),
           static_cast<unsigned> (enc.base ()),
           static_cast<unsigned> (dec.size () / 1024),
           static_cast<unsigned> (dec.base ()));
    backend = spi->backend ();
  }
  linef ("spi-pipeline: fs done\n");

  const std::uint32_t enc_len = pipeline::encoded_total (src_len);
  const std::uint32_t crc0 = flatfs::crc32 (image (), src_len);
  pipeline::g_run.crc0 = crc0;

  const bool banner_ok = banner (backend, src_len);
  linef ("spi-pipeline: image %lu bytes at 0x%08x, source %lu%s, crc0 0x%08x\n",
         static_cast<unsigned long> (full),
         static_cast<unsigned> (IMAGE_BASE),
         static_cast<unsigned long> (src_len),
         src_len == full ? "" : " (clamped: no flash part)",
         static_cast<unsigned> (crc0));
  linef ("spi-pipeline: %lu chunks of %u (last %lu), %u encoded, %u ms per chunk\n",
         static_cast<unsigned long> (pipeline::chunks (src_len)),
         static_cast<unsigned> (pipeline::CHUNK),
         static_cast<unsigned long> (pipeline::chunk_len (
             src_len, pipeline::chunks (src_len) - 1)),
         static_cast<unsigned> (enc_len),
         static_cast<unsigned> (pipeline::pace (src_len)));

  // ---- the outer loop ------------------------------------------------------

  std::size_t iterations_ok = 0, chunks_ok = 0, lengths_ok = 0, crcs_ok = 0,
              cycles_ok = 0;

  for (std::size_t iter = 0; iter < pipeline::ITERATIONS; ++iter)
    {
      pipeline::reset ();

      t3_args a3;
      t4_args a4;
      t5_args a5;
      std::snprintf (a3.enc_name, sizeof (a3.enc_name), "enc%lu.b64",
                     static_cast<unsigned long> (iter));
      a3.src_len = src_len;
      a4.iter = iter;
      std::snprintf (a4.enc_name, sizeof (a4.enc_name), "enc%lu.b64",
                     static_cast<unsigned long> (iter));
      std::snprintf (a5.enc_name, sizeof (a5.enc_name), "enc%lu.b64",
                     static_cast<unsigned long> (iter));
      std::snprintf (a5.dec_name, sizeof (a5.dec_name), "dec%lu.bin",
                     static_cast<unsigned long> (iter));
      a5.src_len = src_len;

      thread::attributes attr = thread::initializer;
      attr.th_stack_size_bytes = pipeline::STACK;

      // Thread objects + stacks come from the heap; the idle thread reaps an
      // exited thread's stack, and join() returns once it has. Delete after
      // join, like the Rust example joins and drops its handles.
      thread* t1 = new thread{ "t1", t1_image_to_buffer,
                               reinterpret_cast<void*> (src_len), attr };
      thread* t2 = new thread{ "t2", t2_base64_encode, nullptr, attr };
      thread* t3 = new thread{ "t3", t3_write_spi, &a3, attr };
      thread* t4 = new thread{ "t4", t4_blink_uart, &a4, attr };
      thread* t5 = new thread{ "t5", t5_decode_verify, &a5, attr };

      // The semihosting witness of the heap, after this iteration's five
      // spawns: the first spawn reaped the previous iteration's exited
      // threads, so this is the steady-state figure.
      os::trace::printf ("[%8lu ms] iter %lu: heap free %u B\n",
                         static_cast<unsigned long> (sysclock.now ()),
                         static_cast<unsigned long> (iter),
                         static_cast<unsigned> (heap_free_bytes ()));

      // The cleanup barrier, then the only correct form of "delete the
      // threads": joining them (spec defect 17).
      (void) pipeline::g_verify_done.wait ();
      t1->join ();
      t2->join ();
      t3->join ();
      t4->join ();
      t5->join ();
      delete t1;
      delete t2;
      delete t3;
      delete t4;
      delete t5;

      const bool this_chunks
          = (pipeline::g_run.chunks == pipeline::chunks (src_len)
             && pipeline::g_run.final_seen);
      const bool this_lengths
          = (pipeline::g_run.enc_bytes == enc_len
             && pipeline::g_run.dec_bytes == static_cast<std::uint32_t> (src_len));
      const bool this_crc = (pipeline::g_run.crc1 == pipeline::g_run.crc0);
      const bool this_cycles
          = (pipeline::g_run.cycles == pipeline::BLINK_CYCLES
             && pipeline::g_run.monotonic
             && pipeline::g_run.longest <= pipeline::g_run.enc_bytes);

      chunks_ok += this_chunks ? 1 : 0;
      lengths_ok += this_lengths ? 1 : 0;
      crcs_ok += this_crc ? 1 : 0;
      cycles_ok += this_cycles ? 1 : 0;
      if (this_chunks && this_lengths && this_crc && this_cycles
          && pipeline::g_run.errors == 0)
        {
          iterations_ok++;
        }

      linef ("[%8lu ms] iter %lu: %u chunks, enc %u B, dec %u B, crc 0x%08x==0x%08x, %u cycles (%u fresh), %u error(s), heap %u B\n",
             static_cast<unsigned long> (sysclock.now ()),
             static_cast<unsigned long> (iter),
             static_cast<unsigned> (pipeline::g_run.chunks),
             static_cast<unsigned> (pipeline::g_run.enc_bytes),
             static_cast<unsigned> (pipeline::g_run.dec_bytes),
             static_cast<unsigned> (pipeline::g_run.crc0),
             static_cast<unsigned> (pipeline::g_run.crc1),
             static_cast<unsigned> (pipeline::g_run.cycles),
             static_cast<unsigned> (pipeline::g_run.fresh),
             static_cast<unsigned> (pipeline::g_run.errors),
             static_cast<unsigned> (heap_free_bytes ()));
    }

  list_files ();

  // ---- the verdicts ------------------------------------------------------

  std::uint32_t failures = 0;

  if (chunks_ok != pipeline::ITERATIONS)
    {
      failures++;
    }
  linef ("pipe 1: %u of %u iterations wrote %u chunks and saw the final one\n",
         static_cast<unsigned> (chunks_ok),
         static_cast<unsigned> (pipeline::ITERATIONS),
         static_cast<unsigned> (pipeline::chunks (src_len)));

  if (lengths_ok != pipeline::ITERATIONS)
    {
      failures++;
    }
  linef ("pipe 2: %u of %u produced %u encoded and %u decoded bytes\n",
         static_cast<unsigned> (lengths_ok),
         static_cast<unsigned> (pipeline::ITERATIONS),
         static_cast<unsigned> (enc_len),
         static_cast<unsigned> (src_len));

  if (crcs_ok != pipeline::ITERATIONS)
    {
      failures++;
    }
  linef ("pipe 3: %u of %u decoded back to crc0 0x%08x\n",
         static_cast<unsigned> (crcs_ok),
         static_cast<unsigned> (pipeline::ITERATIONS),
         static_cast<unsigned> (crc0));

  if (cycles_ok != pipeline::ITERATIONS)
    {
      failures++;
    }
  linef ("pipe 4: %u of %u ran %u blink cycles with snapshots that never shrank\n",
         static_cast<unsigned> (cycles_ok),
         static_cast<unsigned> (pipeline::ITERATIONS),
         static_cast<unsigned> (pipeline::BLINK_CYCLES));

  if (!codec_ok)
    {
      failures++;
    }
  linef ("pipe 5: base64 matches the RFC 4648 vectors: %s\n",
         codec_ok ? "true" : "false");

  if (!banner_ok)
    {
      failures++;
    }
  linef ("pipe 6: banner printed 4 lines at console-lock depth 2: %s\n",
         banner_ok ? "true" : "false");

  linef ("spi-pipeline: %u of %u iterations clean, heap %u bytes\n",
         static_cast<unsigned> (iterations_ok),
         static_cast<unsigned> (pipeline::ITERATIONS),
         static_cast<unsigned> (heap_free_bytes ()));
  if (failures == 0 && iterations_ok == pipeline::ITERATIONS)
    {
      linef ("spi-pipeline: PASS\n");
    }
  else
    {
      linef ("spi-pipeline: FAIL, %u verdicts\n", static_cast<unsigned> (failures));
    }

  // End the run with the verdict (semihosting exit).
  {
    const bool ok = (failures == 0 && iterations_ok == pipeline::ITERATIONS);
    linef ("RESULT: %s\n", ok ? "PASS" : "FAIL");
    if (ok)
      {
        hw_result::ok ();
      }
    else
      {
        hw_result::fail ();
      }
  }

  // Park: the workers are done; the board keeps the scheduler alive.
  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
