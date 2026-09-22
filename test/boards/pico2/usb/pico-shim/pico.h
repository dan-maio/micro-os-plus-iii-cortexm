/* pico.h — minimal pico-sdk shim for the vendored TinyUSB RP2040/RP2350 dcd. */
#ifndef _MU_PICO_SHIM_PICO_H
#define _MU_PICO_SHIM_PICO_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

/* This shim targets the RP2350 (Cortex-M33). Selecting the non-RP2040 path
 * disables the RP2040 enumeration/E15 errata workarounds. */
#ifndef PICO_RP2350
#define PICO_RP2350 1
#endif

#define __not_in_flash(group)
#define __not_in_flash_func(f) f
#define __no_inline_not_in_flash_func(f) f
#ifndef __force_inline
#define __force_inline inline __attribute__ ((always_inline))
#endif
#ifndef __unused
#define __unused __attribute__ ((unused))
#endif
#ifndef count_of
#define count_of(a) (sizeof (a) / sizeof ((a)[0]))
#endif

#ifdef __cplusplus
extern "C"
{
#endif
  void mu_usb_panic (const char* msg);
  uint8_t rp2040_chip_version (void);
#ifdef __cplusplus
}
#endif
#define panic(...) mu_usb_panic (__FILE__)
#ifndef hard_assert
#define hard_assert(x) assert (x)
#endif

#endif
