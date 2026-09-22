/*
 * address_mapped.h — minimal pico-sdk shim for the vendored TinyUSB RP2040
 * device driver, providing the io types and the RP2040/RP2350 atomic register
 * aliases (SET/CLR/XOR at +0x2000/+0x3000/+0x1000). Self-contained; no rest of
 * pico-sdk needed. See src/usb-hid/README.md.
 */
#ifndef _MU_PICO_SHIM_ADDRESS_MAPPED_H
#define _MU_PICO_SHIM_ADDRESS_MAPPED_H

#include <stdint.h>
#include "hardware/regs/addressmap.h"
#ifndef _u
#define _u(x) x ## u
#endif

typedef volatile uint32_t io_rw_32;
typedef const volatile uint32_t io_ro_32;
typedef volatile uint32_t io_wo_32;
typedef volatile uint16_t io_rw_16;
typedef const volatile uint16_t io_ro_16;
typedef volatile uint16_t io_wo_16;
typedef volatile uint8_t io_rw_8;
typedef const volatile uint8_t io_ro_8;
typedef volatile uint8_t io_wo_8;

// Register-annotation marker used inside the vendored struct headers; no-op.
#define _REG_(x)

// Struct-layout self-checks in the vendored headers; make them no-ops.
#define check_hw_layout(type, member, offset)
#define check_hw_size(type, size)

// RP2040/RP2350 atomic register aliases.
#define REG_ALIAS_RW_BITS (0x0u << 12)
#define REG_ALIAS_XOR_BITS (0x1u << 12)
#define REG_ALIAS_SET_BITS (0x2u << 12)
#define REG_ALIAS_CLR_BITS (0x3u << 12)

#ifdef __cplusplus
extern "C"
{
#endif

  __attribute__ ((always_inline)) static inline void
  hw_set_bits (io_rw_32* reg, uint32_t mask)
  {
    *(io_rw_32*) ((uintptr_t) reg | REG_ALIAS_SET_BITS) = mask;
  }
  __attribute__ ((always_inline)) static inline void
  hw_clear_bits (io_rw_32* reg, uint32_t mask)
  {
    *(io_rw_32*) ((uintptr_t) reg | REG_ALIAS_CLR_BITS) = mask;
  }
  __attribute__ ((always_inline)) static inline void
  hw_xor_bits (io_rw_32* reg, uint32_t mask)
  {
    *(io_rw_32*) ((uintptr_t) reg | REG_ALIAS_XOR_BITS) = mask;
  }
  __attribute__ ((always_inline)) static inline void
  hw_write_masked (io_rw_32* reg, uint32_t values, uint32_t write_mask)
  {
    hw_xor_bits (reg, (*reg ^ values) & write_mask);
  }
  // Pointer to the SET/CLR alias of a register block (used by the dcd).
  __attribute__ ((always_inline)) static inline io_rw_32*
  hw_set_alias (volatile void* p)
  {
    return (io_rw_32*) ((uintptr_t) p | REG_ALIAS_SET_BITS);
  }
  __attribute__ ((always_inline)) static inline io_rw_32*
  hw_clear_alias (volatile void* p)
  {
    return (io_rw_32*) ((uintptr_t) p | REG_ALIAS_CLR_BITS);
  }
  __attribute__ ((always_inline)) static inline void*
  hw_set_alias_untyped (volatile void* p)
  {
    return (void*) ((uintptr_t) p | REG_ALIAS_SET_BITS);
  }
  __attribute__ ((always_inline)) static inline void*
  hw_clear_alias_untyped (volatile void* p)
  {
    return (void*) ((uintptr_t) p | REG_ALIAS_CLR_BITS);
  }
  __attribute__ ((always_inline)) static inline io_rw_32*
  hw_xor_alias (volatile void* p)
  {
    return (io_rw_32*) ((uintptr_t) p | REG_ALIAS_XOR_BITS);
  }

#ifdef __cplusplus
}
#endif

#endif /* _MU_PICO_SHIM_ADDRESS_MAPPED_H */
