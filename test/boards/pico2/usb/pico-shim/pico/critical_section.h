/* critical_section.h — shim: a per-core PRIMASK critical section. Valid because
 * the USB device task + USB IRQ are pinned to one core (see usb-hid glue). */
#ifndef _MU_PICO_SHIM_CRITICAL_SECTION_H
#define _MU_PICO_SHIM_CRITICAL_SECTION_H
#include <stdint.h>
typedef struct { uint32_t saved; } critical_section_t;
#ifdef __cplusplus
extern "C" {
#endif
void critical_section_init(critical_section_t* c);
void critical_section_deinit(critical_section_t* c);
void critical_section_enter_blocking(critical_section_t* c);
void critical_section_exit(critical_section_t* c);
#ifdef __cplusplus
}
#endif
#endif
