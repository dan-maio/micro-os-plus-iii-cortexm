/* pico.h — minimal pico-sdk shim for the vendored TinyUSB RP2040 dcd, for
 * the RP2040 (pico board). Only the files that differ from the Pico 2's shim
 * (test/boards/pico2/usb/pico-shim/) are here; this directory is searched
 * first, so the rest -- the SDK's USB register and struct headers -- are the
 * Pico 2's. */
#ifndef _MU_PICO_SHIM_PICO_H
#define _MU_PICO_SHIM_PICO_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

/* Neither PICO_RP2040 nor PICO_RP2350 is defined: TinyUSB takes the same path
 * as on the Pico 2, plus the RP2040-E2 workaround, switched on here
 * (rp2040_usb.h only defaults it): the endpoint ABORT flag is unusable on
 * B0/B1, and TinyUSB skips it when rp2040_chip_version() < 2 (pico-shim.c
 * reads the real revision).
 *
 * E4 is host-only; E5 (enumeration on a busy bus) needs the SDK's pico_fix,
 * which this shim does not carry; E15 matters only for bulk IN behind some
 * USB hubs, and the tests plug the board in directly. */
#define CFG_TUSB_RP2_ERRATA_E2 1

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
