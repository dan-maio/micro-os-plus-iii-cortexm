/* system_rp2350.c — CMSIS SystemCoreClock for the RP2350. */
#include <stdint.h>

#if !defined(PICO2_SYS_CLK_KHZ)
#define PICO2_SYS_CLK_KHZ 150000
#endif

uint32_t SystemCoreClock = (PICO2_SYS_CLK_KHZ * 1000U);

void
SystemInit (void)
{
  /* Clock tree is brought up by the BSP (clocks::init) before the RTOS. */
}

void
SystemCoreClockUpdate (void)
{
  SystemCoreClock = (PICO2_SYS_CLK_KHZ * 1000U);
}
