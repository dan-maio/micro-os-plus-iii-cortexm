#ifndef _MU_PICO_SHIM_RESETS_H
#define _MU_PICO_SHIM_RESETS_H
#include <stdint.h>
#define RESETS_RESET_USBCTRL_BITS 0x01000000u   /* bit 24 (RP2040) */
#ifdef __cplusplus
extern "C" {
#endif
void reset_block(uint32_t bits);           /* assert reset for the block(s) */
void unreset_block_wait(uint32_t bits);    /* deassert + wait until ready */
#ifdef __cplusplus
}
#endif
#endif
