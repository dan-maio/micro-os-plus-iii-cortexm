/*
 * flatfs.hpp — a tiny append-only flat-file archive ("flatfs") on the SPI
 * NOR flash, for the WeAct Studio F411CE.
 *
 * On-disk format is byte-for-byte the `fs-archive` used by the WeAct
 * reference firmware (`fs-archive-spi.cpp`) and the Rust board crate:
 *
 *   flash +0x0000  FsSuper  { magic 0x46414C31, version 1, toc_off 32,
 *                             toc_cap 64, blob_off 2080, reserved }  (32 B)
 *   flash +0x0020  64 × FsEntry  { name[16], off, size, crc32, state }  (32 B)
 *   flash +0x0820  blobs, each 4-byte aligned, appended at a frontier
 *
 * An entry's `state` is 0x4C495645 ("LIVE") or 0xFFFFFFFF ("FREE", erased).
 * There is no deletion: files are added until the TOC or the region fills.
 *
 * [Partition] names one region by { base, size }. The superblock records no
 * size — how far a partition reaches is a property of the Partition value,
 * checked on every read that could otherwise walk out of the end.
 *
 * [Writer] streams a blob at the frontier; `finish` publishes the TOC entry.
 * Nothing is visible to find/count until finish.
 */

#ifndef WEACT_F411CE_FLATFS_HPP_
#define WEACT_F411CE_FLATFS_HPP_

#include <cstdint>
#include <cstddef>

namespace flatfs
{
  // ---- constants (shared with the C++ reference and the Rust port) -------

  inline constexpr std::uint32_t REGION_BASE = 0U;
  inline constexpr std::uint32_t REGION_SIZE = 1024U * 1024U;
  inline constexpr std::uint32_t MAGIC = 0x4641'4C31U;
  inline constexpr std::uint32_t VERSION = 1U;
  inline constexpr std::uint32_t TOC_CAP = 64U;
  inline constexpr std::uint32_t NAME_MAX = 16U;
  inline constexpr std::uint32_t STATE_FREE = 0xFFFF'FFFFU;
  inline constexpr std::uint32_t STATE_LIVE = 0x4C49'5645U;
  inline constexpr std::uint32_t SUPER_SIZE = 32U;
  inline constexpr std::uint32_t ENTRY_SIZE = 32U;
  inline constexpr std::uint32_t BLOB_OFF = 2080U;

  // ---- CRC-32 (IEEE 802.3 / zlib, as the C++ reference computes it) ------
  //
  // Bitwise rather than table-driven: the flash read it verifies is three
  // orders of magnitude slower than the arithmetic.

  inline constexpr std::uint32_t CRC32_INIT = 0xFFFF'FFFFU;

  std::uint32_t
  crc32_update (std::uint32_t crc, const std::uint8_t* data, std::size_t len);

  inline std::uint32_t
  crc32_final (std::uint32_t crc)
  {
    return ~crc;
  }

  std::uint32_t
  crc32 (const std::uint8_t* data, std::size_t len);

  // ---- one parsed TOC entry ----------------------------------------------

  struct Entry
  {
    std::uint8_t name[NAME_MAX];
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t crc;
    std::uint32_t state;

    bool
    is_live () const
  {
    return state == STATE_LIVE;
  }

    std::size_t
    name_len () const
  {
    std::size_t i = 0;
    while (i < NAME_MAX && name[i] != 0)
      {
        ++i;
      }
    return i;
  }
  };

  // ---- errors -------------------------------------------------------------

  enum class error
  {
    none,
    not_formatted,
    bad_name,
    toc_full,
    no_space,
    buffer_too_small,
    corrupt,
    flash_timeout,
  };

  const char*
  error_str (error e);

  // ---- one archive region -------------------------------------------------

  class Partition
  {
  public:
    constexpr Partition (std::uint32_t base, std::uint32_t size)
        : base_{ base }, size_{ size }
    {
      // a partition starts on a 4 KiB sector boundary
      // a partition is a whole number of 4 KiB sectors
      // a partition must reach past its own table of contents
    }

    static Partition
    primary ()
  {
    return Partition{ REGION_BASE, REGION_SIZE };
  }

    std::uint32_t
    base () const
  {
    return base_;
  }

    std::uint32_t
    size () const
  {
    return size_;
  }

    // Whether a valid archive superblock is present.
    bool
    valid () const;

    // The number of live files.
    std::uint32_t
    count () const;

    // The `index`-th live file (0-based, in slot order), if any.
    bool
    entry (std::uint32_t index, Entry& out) const;

    // The live entry whose name matches `name` (name_len bytes), if any.
    bool
    find (const char* name, std::size_t name_len, Entry& out) const;

    // Read up to out_len bytes of a live entry's blob starting `at` bytes in.
    // Returns bytes read (0 at/past the end), or -1 on error.
    int
    read_blob_at (const Entry& e, std::uint32_t at, std::uint8_t* out,
                  std::size_t out_len) const;

    // The number of partition bytes currently used (the blob frontier).
    std::uint32_t
    used () const;

    // Erase the whole partition and write a fresh empty superblock.
    error
    format () const;

    // Append a whole blob as a new file named `name`.
    error
    add (const char* name, std::size_t name_len, const std::uint8_t* data,
         std::size_t data_len) const;

    class Writer;

    // Open a file whose bytes arrive over time. One writer per partition at
    // a time.
    error
    create (const char* name, std::size_t name_len, Writer& w) const;

  private:
    std::uint32_t base_;
    std::uint32_t size_;

    bool
    read_super_valid () const;

    std::uint32_t
    frontier () const;

    void
    read_entry_raw (std::uint32_t slot, std::uint8_t (&b)[ENTRY_SIZE]) const;
  };

  class Partition::Writer
  {
  public:
    Writer () = default;

    // Append data to the file.
    error
    write (const std::uint8_t* data, std::size_t len);

    // Bytes written so far.
    std::uint32_t
    len () const
  {
    return len_;
  }

    // Read back up to out_len bytes of what has been written, starting `at`.
    std::size_t
    read_back (std::uint32_t at, std::uint8_t* out, std::size_t out_len) const;

    // Publish the TOC entry, making the file visible.
    error
    finish ();

    // The partition this file is being written to.
    std::uint32_t
    partition_base () const
  {
    return part_.base_;
  }

  private:
    friend class Partition;
    Partition part_{ 0, 0 }; // a copy, so the writer outlives the create call
    std::uint8_t name_[NAME_MAX] = { 0 };
    std::size_t name_len_ = 0;
    std::uint32_t slot_ = 0;
    std::uint32_t offset_ = 0;
    std::uint32_t len_ = 0;
    std::uint32_t crc_ = CRC32_INIT;
  };

  // ---- the primary partition, as free functions --------------------------
  // (kept for flatfs-demo / provisioning parity; the pipeline example uses
  // its own two partitions)

  bool
  valid ();
  std::uint32_t
  count ();
  std::uint32_t
  used ();
  error
  format ();
  error
  add (const char* name, std::size_t name_len, const std::uint8_t* data,
       std::size_t data_len);
}

#endif /* WEACT_F411CE_FLATFS_HPP_ */
