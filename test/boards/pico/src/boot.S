/******************************************************************************
 * boot.S - RP2040 (Cortex-M0+) startup for the µOS++ Raspberry Pi Pico port.
 *
 * The Pico 2 board's boot.S (test/boards/pico2/src/boot.S), for the RP2040
 * boot flow and the ARMv6-M instruction set (Thumb-1: no post-increment
 * ldr/str, no cbz, no 32-bit immediates):
 *
 *   The RP2040 bootrom loads the first 256 bytes of flash -- boot2.S, the
 *   second stage that sets the QSPI flash up for XIP -- and boot2 then enters
 *   the image through the vector table that follows it, at 0x10000100: VTOR,
 *   MSP and the reset vector. There is no PICOBIN block on the RP2040.
 *
 *   1. _entry_point   - debugger entry: re-enter the bootrom (redo XIP setup).
 *   2. _reset_handler - core-0-only C runtime: VTOR, .data/.bss,
 *                       __libc_init_array (C++ ctors), main().
 *   3. Weak default exception handlers.
 ******************************************************************************/

.syntax unified
.cpu cortex-m0plus
.thumb

/* ---- 1) ELF entry point (debugger) ------------------------------------- */
.section .reset, "ax"
.thumb_func
.global _entry_point
.type _entry_point, %function
_entry_point:
    movs r0, #0                   /* bootrom vector table base */
    ldr r1, =0xe000ed08           /* VTOR */
    str r0, [r1]
    ldmia r0!, {r1, r2}           /* r1 = initial MSP, r2 = bootrom reset */
    msr msp, r1
    bx r2

/* ---- 2) Reset handler (core 0 only) ------------------------------------ */
.thumb_func
.global _reset_handler
.type _reset_handler, %function
_reset_handler:
    /* Core 1 (if it ever reaches here) is parked back in the bootrom. */
    ldr r0, =0xd0000000           /* SIO_CPUID */
    ldr r0, [r0]
    cmp r0, #0
    beq 1f
    movs r0, #0
    ldr r1, =0xe000ed08
    str r0, [r1]
    ldmia r0!, {r1, r2}
    msr msp, r1
    bx r2
1:
    /* Release all 32 SIO spinlocks (0xd0000100..0xd000017c; a write frees
     * one). Their state survives a debugger reset or re-flash, so a run that
     * ended while holding one -- spinlock 0 is the kernel lock -- would leave
     * the next image spinning at its first critical section. */
    ldr r0, =0xd0000100           /* SIO_SPINLOCK0 */
    movs r1, #32
7:  str r1, [r0]
    adds r0, r0, #4
    subs r1, r1, #1
    bne 7b

    /* VTOR -> our vector table (boot2 already set it on a flash boot; a RAM
     * run started by the debugger may not have) */
    ldr r0, =__vector_table
    ldr r1, =0xe000ed08
    str r0, [r1]
    dsb
    isb

    ldr r0, =__StackTop
    mov sp, r0

    /* Copy .data from flash (__etext) into RAM. In a RAM-run image the two
     * addresses are the same and the copy rewrites the words in place. */
    ldr r0, =__etext
    ldr r1, =__data_start__
    ldr r2, =__data_end__
    b 3f
2:  ldmia r0!, {r3}
    stmia r1!, {r3}
3:  cmp r1, r2
    blo 2b

    /* Zero .bss */
    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    movs r2, #0
    b 5f
4:  stmia r0!, {r2}
5:  cmp r0, r1
    blo 4b

    /* Run C++ static constructors (.preinit_array + .init_array) */
    bl __libc_init_array

    /* Enter the application / kernel main() with argc=0, argv=NULL (no
     * semihosting args), as on the Pico 2. */
    movs r0, #0
    movs r1, #0
    bl main

6:  b 6b

/* ---- 3) Weak default handlers ------------------------------------------ */
/* Each spins on a local label, not on its own symbol: when a strong handler
 * replaces the weak one, `b \name` would resolve to it, out of a Thumb-1
 * branch's +-2 KB reach. */
.macro default_handler name
    .thumb_func
    .global \name
    .weak \name
    .type \name, %function
\name:
1:  b 1b
.endm

default_handler NMI_Handler
default_handler HardFault_Handler
default_handler SVC_Handler
default_handler PendSV_Handler
default_handler SysTick_Handler
default_handler SIO_IRQ_PROC0_Handler
default_handler SIO_IRQ_PROC1_Handler
default_handler USBCTRL_IRQ_Handler

.thumb_func
.global default_irq_handler
.weak default_irq_handler
.type default_irq_handler, %function
default_irq_handler:
    b default_irq_handler

/* Weak per-IRQ handler slots: a test can install a strong handler (e.g.
 * smp-test5's UART0_IRQ_Handler for interrupt-driven TX) without touching
 * interrupt-vectors.S; without one the IRQ falls back to the stub. */
.thumb_func
.global UART0_IRQ_Handler
.weak UART0_IRQ_Handler
.type UART0_IRQ_Handler, %function
.thumb_set UART0_IRQ_Handler, default_irq_handler

/* IRQ 0 (the validator's TC_ThreadInterrupts). Weak so only the validator's
 * strong WDT_IRQHandler overrides it; everything else keeps the stub. */
.thumb_func
.global WDT_IRQHandler
.weak WDT_IRQHandler
.type WDT_IRQHandler, %function
.thumb_set WDT_IRQHandler, default_irq_handler

.thumb_func
.global TIMER_IRQ_2_Handler
.weak TIMER_IRQ_2_Handler
.type TIMER_IRQ_2_Handler, %function
.thumb_set TIMER_IRQ_2_Handler, default_irq_handler

.thumb_func
.global TIMER_IRQ_3_Handler
.weak TIMER_IRQ_3_Handler
.type TIMER_IRQ_3_Handler, %function
.thumb_set TIMER_IRQ_3_Handler, default_irq_handler
