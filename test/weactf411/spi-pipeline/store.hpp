/*
 * store.hpp — where the pipeline's two files go, and the two backends.
 *
 * Two partitions on the SPI NOR flash (encoded-file partition, decoded-file
 * partition), sized at run time from the source length and ITERATIONS, high
 * in the part so nothing disturbs the primary archive. Under QEMU, which
 * models no SPI flash, a small RAM disk of two files stands in — chosen at
 * run time by the JEDEC ID, exactly as the Rust reference does.
 *
 * The device is owned by a mutex: every thread reaches it through a Guard,
 * so the spec's "only touch the file under mutex_spi" is enforced by the
 * type rather than by review.
 */

#ifndef SPI_PIPELINE_STORE_HPP_
#define SPI_PIPELINE_STORE_HPP_

#include <cstdint>
#include <cstddef>

#include <cmsis-plus/rtos/os.h>

#include <flash.hpp>
#include <flatfs.hpp>

namespace store
{
  // Where the app's partitions begin: just past the 1 MiB primary archive.
  inline constexpr std::uint32_t APP_BASE = 0x0010'0000U;

  // The most source the RAM backend will take (deliberately not a multiple
  // of 256, so the short-chunk and '==' paths run in the QEMU gate too).
  inline constexpr std::size_t RAM_SOURCE_MAX = 8000;

  enum class Which
  {
    encoded,
    decoded,
  };

  const char*
  which_label (Which w);

  // The RAM disk's per-file capacity: the encoded file, the larger of the two
  // by four thirds. encoded_total(8000) = 31*344 + 88 = 10752.
  inline constexpr std::size_t RAM_CAP = 10752;

  struct RamFile
  {
    char name[flatfs::NAME_MAX];
    std::size_t name_len;
    std::size_t len;
    bool live;
    std::uint8_t bytes[RAM_CAP];

    bool
    named (const char* n, std::size_t nlen) const
  {
    return live && name_len == nlen && __builtin_memcmp (name, n, nlen) == 0;
  }
  };

  struct Jedec
  {
    std::uint8_t manufacturer;
    std::uint8_t mem_type;
    std::uint8_t capacity;
  };

  class Storage
  {
  public:
    Storage ();

    bool
    probe ();
    void
    configure (std::size_t src_len);
    flatfs::Partition
    partition (Which w) const;
    Jedec
    jedec () const;
    const char*
    backend () const;

    // 0 == ok, < 0 == error. Errors carry a small negative code so the caller
    // can name them; the transcript only cares whether they are zero.
    int
    format ();
    int
    create (Which w, const char* name, std::size_t name_len);
    int
    write (const std::uint8_t* data, std::size_t len);
    std::uint32_t
    open_len () const;
    int
    close ();

    // Read a window of a closed file; returns bytes read, or a negative error.
    int
    read_file (Which w, const char* name, std::size_t name_len,
               std::uint32_t at, std::uint8_t* out, std::size_t out_len) const;
    // A closed file's length, or 0 if not there.
    std::uint32_t
    file_len (Which w, const char* name, std::size_t name_len) const;
    // The length as an observer sees it now (open writer or published file).
    std::uint32_t
    visible_len (Which w, const char* name, std::size_t name_len) const;
    // Read a window of a file, open or closed; returns bytes read.
    std::size_t
    read_visible (Which w, const char* name, std::size_t name_len,
                  std::uint32_t at, std::uint8_t* out,
                  std::size_t out_len) const;

    // Call fn(name, name_len, size, crc) for every file on a partition.
    template <typename F>
    void
    list (Which w, F&& fn) const
    {
      if (on_flash_)
        {
          flatfs::Partition p = partition (w);
          for (std::uint32_t i = 0; i < p.count (); ++i)
            {
              flatfs::Entry e;
              if (p.entry (i, e))
                {
                  fn (reinterpret_cast<const char*> (e.name),
                      e.name_len (), e.size, e.crc);
                }
            }
        }
      else
        {
          const RamFile& f = ram_[slot (w)];
          if (f.live)
            {
              fn (f.name, f.name_len, static_cast<std::uint32_t> (f.len),
                  flatfs::crc32 (f.bytes, f.len));
            }
        }
    }

    std::uint32_t
    used (Which w) const;

  private:
    bool on_flash_;
    Jedec jedec_;
    bool open_;
    Which open_which_;
    flatfs::Partition::Writer writer_;
    RamFile ram_[2];
    flatfs::Partition enc_part_;
    flatfs::Partition dec_part_;

    static std::size_t
    slot (Which w)
  {
    return (w == Which::encoded) ? 0U : 1U;
  }

    std::size_t
    read_open (std::uint32_t at, std::uint8_t* out, std::size_t out_len) const;

    bool
    open_is (Which w) const;
  };

  // ---- the device mutex + instance ----------------------------------------

  os::rtos::mutex&
  mutex ();

  Storage&
  instance ();
}

#endif /* SPI_PIPELINE_STORE_HPP_ */
