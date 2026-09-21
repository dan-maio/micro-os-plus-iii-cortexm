/*
 * base64.hpp — Base64, RFC 4648 §4 (standard alphabet, with padding).
 *
 * The pipeline encodes each 256-byte chunk independently, so the encoded
 * file is a concatenation of padded groups, not one continuous stream.
 * decode() therefore rejects padding anywhere but the last group of the
 * slice it is given: the decoder must be handed one chunk at a time.
 */

#ifndef SPI_PIPELINE_BASE64_HPP_
#define SPI_PIPELINE_BASE64_HPP_

#include <cstdint>
#include <cstddef>

namespace base64
{
  // Encoded length of n raw bytes, padding included.
  constexpr std::size_t
  encoded_len (std::size_t n)
  {
    return ((n + 2U) / 3U) * 4U;
  }

  // Encode src into dst (must hold encoded_len(src_len) bytes); returns the
  // number of bytes written.
  std::size_t
  encode (const std::uint8_t* src, std::size_t src_len, std::uint8_t* dst);

  // Decode src into dst (capacity dst_cap); returns the number of bytes
  // written, or NPOS for anything not one well-formed encoding or an output
  // that would not fit.
  std::size_t
  decode (const std::uint8_t* src, std::size_t src_len, std::uint8_t* dst,
          std::size_t dst_cap);

  inline constexpr std::size_t NPOS = static_cast<std::size_t> (-1);

  // The RFC 4648 §10 test vectors, both ways.
  bool
  self_test ();
}

#endif /* SPI_PIPELINE_BASE64_HPP_ */
