#ifndef _MU_PICO_SHIM_TIMER_H
#define _MU_PICO_SHIM_TIMER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t time_us_32(void);
void busy_wait_at_least_cycles(uint32_t cycles);
#ifdef __cplusplus
}
#endif
#endif
