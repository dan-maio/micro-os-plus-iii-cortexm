/*
 * flatfs.cpp — archive layer on the SPI NOR flash, for the WeAct F411CE.
 *
 * Ported from the Rust `flatfs`/`flatfs_codec` modules (format byte-for-byte
 * the C++ `fs-archive` reference). Partitions let several independent
 * archives coexist on the same part; the pipeline example uses two of its
 * own, high in the flash, and never touches the primary region at offset 0.
 */

#include <flatfs.hpp>

#include <flash.hpp>

#include <cstring>

namespace
{
  void
  le32 (std::uint32_t v, std::uint8_t* p)
  {
    p[0] = static_cast<std::uint8_t> (v);
    p[1] = static_cast<std::uint8_t> (v >> 8);
    p[2] = static_cast<std::uint8_t> (v >> 16);
    p[3] = static_cast<std::uint8_t> (v >> 24);
  }

  std::uint32_t
  rd32 (const std::uint8_t* p)
  {
    return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8)
           | (static_cast<std::uint32_t> (p[2]) << 16)
           | (static_cast<std::uint32_t> (p[3]) << 24);
  }

  std::uint32_t
  align4 (std::uint32_t v)
  {
    return (v + 3U) & ~3U;
  }

  struct Super
  {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t toc_off;
    std::uint32_t toc_cap;
    std::uint32_t blob_off;
  };

  void
  build_super (std::uint8_t (&s)[flatfs::SUPER_SIZE])
  {
    std::memset (s, 0, sizeof (s));
    le32 (flatfs::MAGIC, s + 0);
    le32 (flatfs::VERSION, s + 4);
    le32 (flatfs::SUPER_SIZE, s + 8);
    le32 (flatfs::TOC_CAP, s + 12);
    le32 (flatfs::BLOB_OFF, s + 16);
  }

  bool
  parse_super (const std::uint8_t* b, Super& s)
  {
    s.magic = rd32 (b + 0);
    s.version = rd32 (b + 4);
    s.toc_off = rd32 (b + 8);
    s.toc_cap = rd32 (b + 12);
    s.blob_off = rd32 (b + 16);
    return true;
  }

  bool
  super_valid_in (const Super& s, std::uint32_t region_size)
  {
    const std::uint32_t first_legal_blob = flatfs::SUPER_SIZE
                                           + flatfs::TOC_CAP
                                                 * flatfs::ENTRY_SIZE;
    return s.magic == flatfs::MAGIC && s.version == flatfs::VERSION
           && s.toc_off == flatfs::SUPER_SIZE
           && s.toc_cap == flatfs::TOC_CAP
           && s.blob_off >= first_legal_blob && s.blob_off < region_size;
  }

  void
  build_entry (const std::uint8_t* name, std::size_t name_len,
               std::uint32_t offset, std::uint32_t size, std::uint32_t crc,
               std::uint8_t (&e)[flatfs::ENTRY_SIZE])
  {
    std::memset (e, 0, sizeof (e));
    std::size_t n = name_len;
    if (n > flatfs::NAME_MAX - 1)
      {
        n = flatfs::NAME_MAX - 1;
      }
    std::memcpy (e, name, n);
    le32 (offset, e + 16);
    le32 (size, e + 20);
    le32 (crc, e + 24);
    le32 (flatfs::STATE_LIVE, e + 28);
  }

  // Parse a TOC slot (infallible: a slot is exactly ENTRY_SIZE bytes).
  void
  parse_entry (const std::uint8_t (&b)[flatfs::ENTRY_SIZE], flatfs::Entry& e)
  {
    std::memcpy (e.name, b, flatfs::NAME_MAX);
    e.offset = rd32 (b + 16);
    e.size = rd32 (b + 20);
    e.crc = rd32 (b + 24);
    e.state = rd32 (b + 28);
  }

  // Compare a stored 16-byte name against a query, up to the first NUL.
  bool
  name_eq (const std::uint8_t (&stored)[flatfs::NAME_MAX],
           const char* query, std::size_t query_len)
  {
    for (std::size_t i = 0; i < flatfs::NAME_MAX; ++i)
      {
        std::uint8_t q = (i < query_len) ? static_cast<std::uint8_t> (query[i])
                                         : 0U;
        if (stored[i] != q)
          {
            return false;
          }
        if (stored[i] == 0)
          {
            return true;
          }
      }
    return true;
  }
}

namespace flatfs
{
  std::uint32_t
  crc32_update (std::uint32_t crc, const std::uint8_t* data, std::size_t len)
  {
    std::uint32_t c = crc;
    for (std::size_t i = 0; i < len; ++i)
      {
        c ^= data[i];
        for (int b = 0; b < 8; ++b)
          {
            c = (c >> 1) ^ (0xEDB8'8320U & (0U - (c & 1U)));
          }
      }
    return c;
  }

  std::uint32_t
  crc32 (const std::uint8_t* data, std::size_t len)
  {
    return crc32_final (crc32_update (CRC32_INIT, data, len));
  }

  const char*
  error_str (error e)
  {
    switch (e)
      {
      case error::none:
        return "none";
      case error::not_formatted:
        return "not formatted";
      case error::bad_name:
        return "bad name";
      case error::toc_full:
        return "toc full";
      case error::no_space:
        return "no space";
      case error::buffer_too_small:
        return "buffer too small";
      case error::corrupt:
        return "corrupt";
      case error::flash_timeout:
        return "flash timeout";
      }
    return "?";
  }

  // ---- Partition ----------------------------------------------------------

  bool
  Partition::valid () const
  {
    return read_super_valid ();
  }

  bool
  Partition::read_super_valid () const
  {
    std::uint8_t b[SUPER_SIZE];
    flash::read (base_, b, sizeof (b));
    Super s;
    parse_super (b, s);
    return super_valid_in (s, size_);
  }

  void
  Partition::read_entry_raw (std::uint32_t slot,
                             std::uint8_t (&b)[ENTRY_SIZE]) const
  {
    const std::uint32_t addr = base_ + SUPER_SIZE + slot * ENTRY_SIZE;
    flash::read (addr, b, sizeof (b));
  }

  std::uint32_t
  Partition::frontier () const
  {
    std::uint8_t b[SUPER_SIZE];
    flash::read (base_, b, sizeof (b));
    Super s;
    parse_super (b, s);
    std::uint32_t f = s.blob_off;
    for (std::uint32_t slot = 0; slot < TOC_CAP; ++slot)
      {
        std::uint8_t raw[ENTRY_SIZE];
        read_entry_raw (slot, raw);
        Entry e;
        parse_entry (raw, e);
        if (e.is_live ())
          {
            std::uint32_t end = e.offset;
            if (e.size > 0xFFFF'FFFFU - end)
              {
                end = 0xFFFF'FFFFU;
              }
            else
              {
                end += e.size;
              }
            if (align4 (end) > f)
              {
                f = align4 (end);
              }
          }
      }
    if (f > size_)
      {
        f = size_;
      }
    return f;
  }

  std::uint32_t
  Partition::count () const
  {
    if (!valid ())
      {
        return 0;
      }
    std::uint32_t n = 0;
    for (std::uint32_t slot = 0; slot < TOC_CAP; ++slot)
      {
        std::uint8_t raw[ENTRY_SIZE];
        read_entry_raw (slot, raw);
        Entry e;
        parse_entry (raw, e);
        if (e.is_live ())
          {
            ++n;
          }
      }
    return n;
  }

  bool
  Partition::entry (std::uint32_t index, Entry& out) const
  {
    if (!valid ())
      {
        return false;
      }
    std::uint32_t seen = 0;
    for (std::uint32_t slot = 0; slot < TOC_CAP; ++slot)
      {
        std::uint8_t raw[ENTRY_SIZE];
        read_entry_raw (slot, raw);
        Entry e;
        parse_entry (raw, e);
        if (e.is_live ())
          {
            if (seen == index)
              {
                out = e;
                return true;
              }
            ++seen;
          }
      }
    return false;
  }

  bool
  Partition::find (const char* name, std::size_t name_len, Entry& out) const
  {
    if (!valid ())
      {
        return false;
      }
    for (std::uint32_t slot = 0; slot < TOC_CAP; ++slot)
      {
        std::uint8_t raw[ENTRY_SIZE];
        read_entry_raw (slot, raw);
        Entry e;
        parse_entry (raw, e);
        if (e.is_live () && name_eq (e.name, name, name_len))
          {
            out = e;
            return true;
          }
      }
    return false;
  }

  int
  Partition::read_blob_at (const Entry& e, std::uint32_t at,
                           std::uint8_t* out, std::size_t out_len) const
  {
    if (!valid () || !e.is_live ())
      {
        return -1; // not_formatted
      }
    if (e.offset > 0xFFFF'FFFFU - e.size || e.offset + e.size > size_)
      {
        return -1; // corrupt
      }
    if (at >= e.size)
      {
        return 0;
      }
    std::size_t left = static_cast<std::size_t> (e.size - at);
    std::size_t n = left < out_len ? left : out_len;
    if (n > 0)
      {
        flash::read (base_ + e.offset + at, out, n);
      }
    return static_cast<int> (n);
  }

  std::uint32_t
  Partition::used () const
  {
    if (!valid ())
      {
        return 0;
      }
    return frontier ();
  }

  error
  Partition::format () const
  {
    for (std::uint32_t a = base_; a < base_ + size_; a += 4096U)
      {
        if (!flash::sector_erase (a))
          {
            return error::flash_timeout;
          }
      }
    std::uint8_t super[SUPER_SIZE];
    build_super (super);
    if (!flash::write (base_, super, sizeof (super)))
      {
        return error::flash_timeout;
      }
    return error::none;
  }

  error
  Partition::add (const char* name, std::size_t name_len,
                  const std::uint8_t* data, std::size_t data_len) const
  {
    Writer w;
    error e = create (name, name_len, w);
    if (e != error::none)
      {
        return e;
      }
    e = w.write (data, data_len);
    if (e != error::none)
      {
        return e;
      }
    return w.finish ();
  }

  error
  Partition::create (const char* name, std::size_t name_len, Writer& w) const
  {
    if (name_len == 0 || name_len >= NAME_MAX)
      {
        return error::bad_name;
      }
    if (!read_super_valid ())
      {
        return error::not_formatted;
      }

    // Find the first free TOC slot.
    std::uint32_t free_slot = TOC_CAP;
    for (std::uint32_t slot = 0; slot < TOC_CAP; ++slot)
      {
        std::uint8_t raw[ENTRY_SIZE];
        read_entry_raw (slot, raw);
        Entry e;
        parse_entry (raw, e);
        if (!e.is_live ())
          {
            free_slot = slot;
            break;
          }
      }
    if (free_slot == TOC_CAP)
      {
        return error::toc_full;
      }

    const std::uint32_t offset = frontier ();
    if (offset >= size_)
      {
        return error::no_space;
      }

    w.part_ = *this;
    std::memset (w.name_, 0, sizeof (w.name_));
    std::memcpy (w.name_, name, name_len);
    w.name_len_ = name_len;
    w.slot_ = free_slot;
    w.offset_ = offset;
    w.len_ = 0;
    w.crc_ = CRC32_INIT;
    return error::none;
  }

  // ---- Writer -------------------------------------------------------------

  error
  Partition::Writer::write (const std::uint8_t* data, std::size_t len)
  {
    if (len == 0)
      {
        return error::none;
      }
    // Checked before programming anything: a rejected write leaves the file
    // exactly as it was.
    if (offset_ > 0xFFFF'FFFFU - len_
        || offset_ + len_ > 0xFFFF'FFFFU - static_cast<std::uint32_t> (len)
        || offset_ + len_ + static_cast<std::uint32_t> (len) > part_.size_)
      {
        return error::no_space;
      }
    if (!flash::write (part_.base_ + offset_ + len_, data, len))
      {
        return error::flash_timeout;
      }
    len_ += static_cast<std::uint32_t> (len);
    crc_ = crc32_update (crc_, data, len);
    return error::none;
  }

  std::size_t
  Partition::Writer::read_back (std::uint32_t at, std::uint8_t* out,
                                std::size_t out_len) const
  {
    if (at >= len_)
      {
        return 0;
      }
    std::size_t left = static_cast<std::size_t> (len_ - at);
    std::size_t n = left < out_len ? left : out_len;
    if (n > 0)
      {
        flash::read (part_.base_ + offset_ + at, out, n);
      }
    return n;
  }

  error
  Partition::Writer::finish ()
  {
    std::uint8_t e[ENTRY_SIZE];
    build_entry (name_, name_len_, offset_, len_, crc32_final (crc_), e);
    const std::uint32_t slot_addr
        = part_.base_ + SUPER_SIZE + slot_ * ENTRY_SIZE;
    if (!flash::page_program (slot_addr, e, sizeof (e)))
      {
        return error::flash_timeout;
      }
    return error::none;
  }

  // ---- primary-partition free functions ----------------------------------

  bool
  valid ()
  {
    return Partition::primary ().valid ();
  }

  std::uint32_t
  count ()
  {
    return Partition::primary ().count ();
  }

  std::uint32_t
  used ()
  {
    return Partition::primary ().used ();
  }

  error
  format ()
  {
    return Partition::primary ().format ();
  }

  error
  add (const char* name, std::size_t name_len, const std::uint8_t* data,
       std::size_t data_len)
  {
    return Partition::primary ().add (name, name_len, data, data_len);
  }
}
