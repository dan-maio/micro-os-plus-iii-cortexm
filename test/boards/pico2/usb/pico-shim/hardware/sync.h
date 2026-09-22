#ifndef _MU_PICO_SHIM_SYNC_H
#define _MU_PICO_SHIM_SYNC_H
#include <stdint.h>
__attribute__((always_inline)) static inline void __dmb(void){ __asm volatile("dmb 0xf":::"memory"); }
__attribute__((always_inline)) static inline void __dsb(void){ __asm volatile("dsb 0xf":::"memory"); }
__attribute__((always_inline)) static inline void __isb(void){ __asm volatile("isb 0xf":::"memory"); }
__attribute__((always_inline)) static inline void __wfe(void){ __asm volatile("wfe"); }
__attribute__((always_inline)) static inline void __sev(void){ __asm volatile("sev"); }
__attribute__((always_inline)) static inline void __wfi(void){ __asm volatile("wfi"); }
__attribute__((always_inline)) static inline void __mem_fence_acquire(void){ __dmb(); }
__attribute__((always_inline)) static inline void __mem_fence_release(void){ __dmb(); }
#define remove_volatile_cast(t, x) (t)(x)
#define remove_volatile_cast_no_barrier(t, x) (t)(x)
#endif
