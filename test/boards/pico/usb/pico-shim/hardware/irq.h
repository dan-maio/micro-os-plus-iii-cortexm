#ifndef _MU_PICO_SHIM_IRQ_H
#define _MU_PICO_SHIM_IRQ_H
#include <stdint.h>
#include <stdbool.h>
#define USBCTRL_IRQ 5    /* RP2040 */
typedef void (*irq_handler_t)(void);
#ifdef __cplusplus
extern "C" {
#endif
void irq_set_enabled(uint32_t num, bool enabled);
void irq_add_shared_handler(uint32_t num, irq_handler_t handler, uint8_t order_priority);
void irq_remove_handler(uint32_t num, irq_handler_t handler);
#define PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY 0x80
#ifdef __cplusplus
}
#endif
#endif
