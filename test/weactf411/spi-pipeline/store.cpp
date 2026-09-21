/*
 * store.cpp — the device layer of the spi-pipeline example. See store.hpp.
 */

#include <store.hpp>

#include <pipeline.hpp>

#include <cstring>

using namespace os::rtos;

namespace
{
  // Layout the two partitions for a source of src_len bytes: each must hold
  // BLOB_OFF front matter plus ITERATIONS accumulated files, rounded to the
  // 4 KiB erase sector.
  std::uint32_t
  round4k (std::uint32_t bytes)
  {
    return ((bytes + 4095U) / 4096U) * 4096U;
  }

  flatfs::Partition
  encoded_partition (std::size_t src_len)
  {
    const std::uint32_t enc_len = pipeline::encoded_total (src_len);
    const std::uint32_t size = round4k (
        flatfs::BLOB_OFF + enc_len * static_cast<std::uint32_t> (pipeline::ITERATIONS));
    return flatfs::Partition{ store::APP_BASE, size };
  }

  flatfs::Partition
  decoded_partition (std::size_t src_len)
  {
    const flatfs::Partition enc = encoded_partition (src_len);
    const std::uint32_t dec_len = static_cast<std::uint32_t> (src_len);
    const std::uint32_t size = round4k (
        flatfs::BLOB_OFF + dec_len * static_cast<std::uint32_t> (pipeline::ITERATIONS));
    return flatfs::Partition{ enc.base () + enc.size (), size };
  }
}

namespace store
{
  const char*
  which_label (Which w)
  {
    return (w == Which::encoded) ? "enc" : "dec";
  }

  Storage::Storage ()
      : on_flash_{ false }, jedec_{ 0, 0, 0 }, open_{ false },
        open_which_{ Which::encoded }, ram_{}, enc_part_{ APP_BASE, 4096 },
        dec_part_{ APP_BASE + 4096, 4096 }
  {
  }

  bool
  Storage::probe ()
  {
    flash::JedecId id = flash::read_id ();
    jedec_.manufacturer = id.manufacturer;
    jedec_.mem_type = id.mem_type;
    jedec_.capacity = id.capacity;
    on_flash_ = (id.manufacturer == 0xEF);
    return on_flash_;
  }

  void
  Storage::configure (std::size_t src_len)
  {
    enc_part_ = encoded_partition (src_len);
    dec_part_ = decoded_partition (src_len);
  }

  flatfs::Partition
  Storage::partition (Which w) const
  {
    return (w == Which::encoded) ? enc_part_ : dec_part_;
  }

  Jedec
  Storage::jedec () const
  {
    return jedec_;
  }

  const char*
  Storage::backend () const
  {
    return on_flash_ ? "SPI flash" : "RAM";
  }

  int
  Storage::format ()
  {
    open_ = false;
    if (on_flash_)
      {
        flatfs::error e = partition (Which::encoded).format ();
        if (e != flatfs::error::none)
          {
            return -1;
          }
        e = partition (Which::decoded).format ();
        if (e != flatfs::error::none)
          {
            return -2;
          }
      }
    else
      {
        for (RamFile& f : ram_)
          {
            f.live = false;
            f.len = 0;
            f.name_len = 0;
          }
      }
    return 0;
  }

  int
  Storage::create (Which w, const char* name, std::size_t name_len)
  {
    if (open_)
      {
        return -100; // a file is already open
      }
    if (on_flash_)
      {
        flatfs::error e = partition (w).create (name, name_len, writer_);
        if (e != flatfs::error::none)
          {
            return -static_cast<int> (e);
          }
        open_ = true;
        open_which_ = w;
        return 0;
      }
    if (name_len >= flatfs::NAME_MAX)
      {
        return -3; // Full
      }
    const std::size_t ix = slot (w);
    RamFile& f = ram_[ix];
    std::memcpy (f.name, name, name_len);
    f.name_len = name_len;
    f.len = 0;
    f.live = false;
    open_ = true;
    open_which_ = w;
    return 0;
  }

  int
  Storage::write (const std::uint8_t* data, std::size_t len)
  {
    if (!open_)
      {
        return -1; // NotOpen
      }
    if (on_flash_)
      {
        flatfs::error e = writer_.write (data, len);
        return e == flatfs::error::none ? 0 : -static_cast<int> (e);
      }
    RamFile& f = ram_[slot (open_which_)];
    if (f.len + len > RAM_CAP)
      {
        return -2; // Full
      }
    std::memcpy (f.bytes + f.len, data, len);
    f.len += len;
    return 0;
  }

  std::uint32_t
  Storage::open_len () const
  {
    if (!open_)
      {
        return 0;
      }
    if (on_flash_)
      {
        return writer_.len ();
      }
    return static_cast<std::uint32_t> (ram_[slot (open_which_)].len);
  }

  int
  Storage::close ()
  {
    if (!open_)
      {
        return -1; // NotOpen
      }
    open_ = false;
    if (on_flash_)
      {
        flatfs::error e = writer_.finish ();
        return e == flatfs::error::none ? 0 : -static_cast<int> (e);
      }
    ram_[slot (open_which_)].live = true;
    return 0;
  }

  bool
  Storage::open_is (Which w) const
  {
    return open_ && open_which_ == w;
  }

  std::size_t
  Storage::read_open (std::uint32_t at, std::uint8_t* out,
                      std::size_t out_len) const
  {
    if (!open_)
      {
        return 0;
      }
    if (on_flash_)
      {
        return writer_.read_back (at, out, out_len);
      }
    const RamFile& f = ram_[slot (open_which_)];
    if (at >= f.len)
      {
        return 0;
      }
    const std::size_t n = (f.len - at) < out_len ? (f.len - at) : out_len;
    std::memcpy (out, f.bytes + at, n);
    return n;
  }

  int
  Storage::read_file (Which w, const char* name, std::size_t name_len,
                      std::uint32_t at, std::uint8_t* out,
                      std::size_t out_len) const
  {
    if (on_flash_)
      {
        flatfs::Partition p = partition (w);
        flatfs::Entry e;
        if (!p.find (name, name_len, e))
          {
            return -1; // NotFound
          }
        int n = p.read_blob_at (e, at, out, out_len);
        return n; // >=0 bytes, or a negative flatfs error
      }
    const RamFile& f = ram_[slot (w)];
    if (!f.named (name, name_len))
      {
        return -1; // NotFound
      }
    if (at >= f.len)
      {
        return 0;
      }
    const std::size_t n = (f.len - at) < out_len ? (f.len - at) : out_len;
    std::memcpy (out, f.bytes + at, n);
    return static_cast<int> (n);
  }

  std::uint32_t
  Storage::file_len (Which w, const char* name, std::size_t name_len) const
  {
    if (on_flash_)
      {
        flatfs::Partition p = partition (w);
        flatfs::Entry e;
        return p.find (name, name_len, e) ? e.size : 0U;
      }
    const RamFile& f = ram_[slot (w)];
    return f.named (name, name_len) ? static_cast<std::uint32_t> (f.len) : 0U;
  }

  std::uint32_t
  Storage::visible_len (Which w, const char* name, std::size_t name_len) const
  {
    if (open_is (w))
      {
        return open_len ();
      }
    return file_len (w, name, name_len);
  }

  std::size_t
  Storage::read_visible (Which w, const char* name, std::size_t name_len,
                         std::uint32_t at, std::uint8_t* out,
                         std::size_t out_len) const
  {
    if (open_is (w))
      {
        return read_open (at, out, out_len);
      }
    int n = read_file (w, name, name_len, at, out, out_len);
    return n < 0 ? 0U : static_cast<std::size_t> (n);
  }

  std::uint32_t
  Storage::used (Which w) const
  {
    if (on_flash_)
      {
        return partition (w).used ();
      }
    return static_cast<std::uint32_t> (ram_[slot (w)].len);
  }

  // ---- the device mutex + instance ----------------------------------------

  namespace
  {
    os::rtos::mutex g_device_mutex{ "spi" };
    Storage g_storage;
  }

  os::rtos::mutex&
  mutex ()
  {
    return g_device_mutex;
  }

  Storage&
  instance ()
  {
    return g_storage;
  }
}
