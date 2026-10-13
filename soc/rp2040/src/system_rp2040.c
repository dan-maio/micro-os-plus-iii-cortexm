/* system_rp2040.c — CMSIS SystemCoreClock for the RP2040. */
#include <stdint.h>

#if !defined(PICO_SYS_CLK_KHZ)
#define PICO_SYS_CLK_KHZ 125000
#endif

uint32_t SystemCoreClock = (PICO_SYS_CLK_KHZ * 1000U);

void
SystemInit (void)
{
  /* Clock tree is brought up by the BSP (clocks::init) before the RTOS. */
}

void
SystemCoreClockUpdate (void)
{
  SystemCoreClock = (PICO_SYS_CLK_KHZ * 1000U);
}
