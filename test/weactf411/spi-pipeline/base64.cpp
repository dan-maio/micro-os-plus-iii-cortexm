/*
 * base64.cpp — Base64, RFC 4648 §4. See base64.hpp.
 */

#include <base64.hpp>

namespace
{
  // 65 includes the NUL of the literal; only [0..63] are the alphabet.
  constexpr std::uint8_t ALPHABET[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  // The 6-bit value of one alphabet char, or 0xFF if not one.
  std::uint8_t
  value (std::uint8_t c)
  {
    if (c >= 'A' && c <= 'Z')
      {
        return c - 'A';
      }
    if (c >= 'a' && c <= 'z')
      {
        return c - 'a' + 26;
      }
    if (c >= '0' && c <= '9')
      {
        return c - '0' + 52;
      }
    if (c == '+')
      {
        return 62;
      }
    if (c == '/')
      {
        return 63;
      }
    return 0xFF;
  }

  constexpr std::uint8_t ALL_VALUES[48] = { 0x00, 0x10, 0x83, 0x10, 0x51,
    0x87, 0x20, 0x92, 0x8B, 0x30, 0xD3, 0x8F, 0x41, 0x14, 0x93, 0x51, 0x55,
    0x97, 0x61, 0x96, 0x9B, 0x71, 0xD7, 0x9F, 0x82, 0x18, 0xA3, 0x92, 0x59,
    0xA7, 0xA2, 0x9A, 0xAB, 0xB2, 0xDB, 0xAF, 0xC3, 0x1C, 0xB3, 0xD3, 0x5D,
    0xB7, 0xE3, 0x9E, 0xBB, 0xF3, 0xDF, 0xBF };
}

namespace base64
{
  std::size_t
  encode (const std::uint8_t* src, std::size_t src_len, std::uint8_t* dst)
  {
    const std::size_t want = encoded_len (src_len);
    std::size_t i = 0;
    std::size_t o = 0;

    while (i + 3 <= src_len)
      {
        const std::uint32_t v = (static_cast<std::uint32_t> (src[i]) << 16)
                                | (static_cast<std::uint32_t> (src[i + 1]) << 8)
                                | static_cast<std::uint32_t> (src[i + 2]);
        dst[o] = ALPHABET[(v >> 18) & 63U];
        dst[o + 1] = ALPHABET[(v >> 12) & 63U];
        dst[o + 2] = ALPHABET[(v >> 6) & 63U];
        dst[o + 3] = ALPHABET[v & 63U];
        i += 3;
        o += 4;
      }

    const std::size_t rem = src_len - i;
    if (rem == 1)
      {
        const std::uint32_t v = static_cast<std::uint32_t> (src[i]) << 16;
        dst[o] = ALPHABET[(v >> 18) & 63U];
        dst[o + 1] = ALPHABET[(v >> 12) & 63U];
        dst[o + 2] = '=';
        dst[o + 3] = '=';
      }
    else if (rem == 2)
      {
        const std::uint32_t v = (static_cast<std::uint32_t> (src[i]) << 16)
                                | (static_cast<std::uint32_t> (src[i + 1]) << 8);
        dst[o] = ALPHABET[(v >> 18) & 63U];
        dst[o + 1] = ALPHABET[(v >> 12) & 63U];
        dst[o + 2] = ALPHABET[(v >> 6) & 63U];
        dst[o + 3] = '=';
      }
    (void) want;
    return encoded_len (src_len);
  }

  std::size_t
  decode (const std::uint8_t* src, std::size_t src_len, std::uint8_t* dst,
          std::size_t dst_cap)
  {
    if ((src_len % 4U) != 0U)
      {
        return NPOS;
      }
    std::size_t o = 0;
    std::size_t i = 0;
    while (i < src_len)
      {
        const std::uint8_t* g = src + i;
        std::uint8_t pad = 0;
        if (g[0] == '=')
          {
            pad++;
          }
        if (g[1] == '=')
          {
            pad++;
          }
        if (g[2] == '=')
          {
            pad++;
          }
        if (g[3] == '=')
          {
            pad++;
          }
        if (pad > 2)
          {
            return NPOS;
          }
        if (pad > 0)
          {
            if (i + 4 != src_len || g[3] != '=')
              {
                return NPOS;
              }
            if (pad == 2 && g[2] != '=')
              {
                return NPOS;
              }
          }

        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 4U - pad; ++k)
          {
            const std::uint8_t v6 = value (g[k]);
            if (v6 == 0xFF)
              {
                return NPOS;
              }
            v = (v << 6) | v6;
          }
        v <<= (6 * pad);

        const std::size_t n = 3 - pad;
        if (o + n > dst_cap)
          {
            return NPOS;
          }
        dst[o] = static_cast<std::uint8_t> ((v >> 16) & 0xFF);
        if (n >= 2)
          {
            dst[o + 1] = static_cast<std::uint8_t> ((v >> 8) & 0xFF);
          }
        if (n >= 3)
          {
            dst[o + 2] = static_cast<std::uint8_t> (v & 0xFF);
          }
        o += n;
        i += 4;
      }
    return o;
  }

  bool
  self_test ()
  {
    // RFC 4648 §10 vectors.
    struct vec
    {
      const std::uint8_t* raw;
      std::size_t raw_len;
      const char* text;
    };
    static const std::uint8_t r0[] = {};
    static const std::uint8_t rf[] = { 'f' };
    static const std::uint8_t rfo[] = { 'f', 'o' };
    static const std::uint8_t rfoo[] = { 'f', 'o', 'o' };
    static const std::uint8_t rfoob[] = { 'f', 'o', 'o', 'b' };
    static const std::uint8_t rfooba[] = { 'f', 'o', 'o', 'b', 'a' };
    static const std::uint8_t rfoobar[] = { 'f', 'o', 'o', 'b', 'a', 'r' };
    const vec VECTORS[7] = { { r0, 0, "" },
                             { rf, 1, "Zg==" },
                             { rfo, 2, "Zm8=" },
                             { rfoo, 3, "Zm9v" },
                             { rfoob, 4, "Zm9vYg==" },
                             { rfooba, 5, "Zm9vYmE=" },
                             { rfoobar, 6, "Zm9vYmFy" } };

    std::uint8_t enc[16];
    std::uint8_t dec[16];
    for (const vec& v : VECTORS)
      {
        const std::size_t n = encode (v.raw, v.raw_len, enc);
        if (n != encoded_len (v.raw_len))
          {
            return false;
          }
        for (std::size_t k = 0; k < n; ++k)
          {
            if (enc[k] != static_cast<std::uint8_t> (v.text[k]))
              {
                return false;
              }
          }
        const std::size_t m = decode (reinterpret_cast<const std::uint8_t*> (v.text), n,
                                      dec, sizeof (dec));
        if (m == NPOS || m != v.raw_len)
          {
            return false;
          }
        for (std::size_t k = 0; k < m; ++k)
          {
            if (dec[k] != v.raw[k])
              {
                return false;
              }
          }
      }

    // Every one of the 64 characters, in order, both ways.
    std::uint8_t wide[64];
    std::uint8_t all_back[48];
    const std::size_t n = encode (ALL_VALUES, sizeof (ALL_VALUES), wide);
    for (std::size_t k = 0; k < 64; ++k)
      {
        if (wide[k] != ALPHABET[k])
          {
            return false;
          }
      }
    const std::size_t m = decode (ALPHABET, 64, all_back, sizeof (all_back));
    if (m == NPOS || m != sizeof (ALL_VALUES))
      {
        return false;
      }
    for (std::size_t k = 0; k < m; ++k)
      {
        if (all_back[k] != ALL_VALUES[k])
          {
            return false;
          }
      }

    // And the rejections.
    std::uint8_t out[16];
    const char* z1 = "Zm9"; // not a multiple of 4
    const char* z2 = "Zm9*"; // not in the alphabet
    const char* z3 = "Z=9v"; // padding not right-aligned
    const char* z4 = "Zg==Zg=="; // padding in a non-final group
    if (decode (reinterpret_cast<const std::uint8_t*> (z1), 3, out,
                sizeof (out))
        != NPOS)
      {
        return false;
      }
    if (decode (reinterpret_cast<const std::uint8_t*> (z2), 4, out,
                sizeof (out))
        != NPOS)
      {
        return false;
      }
    if (decode (reinterpret_cast<const std::uint8_t*> (z3), 4, out,
                sizeof (out))
        != NPOS)
      {
        return false;
      }
    if (decode (reinterpret_cast<const std::uint8_t*> (z4), 8, out,
                sizeof (out))
        != NPOS)
      {
        return false;
      }
    // output would not fit: 6 raw bytes need 6, give 4.
    if (decode (reinterpret_cast<const std::uint8_t*> ("Zm9vYmFy"), 8, out, 4)
        != NPOS)
      {
        return false;
      }
    return true;
  }
}
