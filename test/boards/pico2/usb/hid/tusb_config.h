/* tusb_config.h — TinyUSB config for the pico2 USB-HID device (RP2350). */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU          OPT_MCU_RP2040   /* rp2040 port also serves RP2350 */
/* pico-sdk placement macros used by tusb_mcu.h before pico.h is seen. */
#ifndef __not_in_flash
#define __not_in_flash(group)
#endif
#ifndef __not_in_flash_func
#define __not_in_flash_func(f) f
#endif

#define CFG_TUSB_OS           OPT_OS_NONE      /* pumped by a µOS++ thread */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__ ((aligned (4)))
#endif

#define CFG_TUD_ENABLED       1
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_HID           1
#define CFG_TUD_CDC           0
#define CFG_TUD_MSC           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_VENDOR        0

/* HID buffer >= the largest report (we forward keystrokes in 64-byte reports). */
#define CFG_TUD_HID_EP_BUFSIZE 64

#endif /* _TUSB_CONFIG_H_ */
