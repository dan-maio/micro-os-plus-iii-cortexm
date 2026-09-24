/******************************************************************************
 * boot.S - RP2350 (Cortex-M33) startup for the µOS++ Pico 2 port.
 *
 * Adapted from the Eclipse C reference (reference/src/boot.S), with C++
 * additions: the reset path runs the C++ static constructors
 * (__libc_init_array) before calling main().
 *
 *   1. PICOBIN image-def block (bootrom requirement, first 4 KB of flash).
 *   2. _entry_point  - debugger entry: re-enter the bootrom (redo XIP setup).
 *   3. _reset_handler - core-0-only C runtime: VTOR, FPU, .data/.bss,
 *                       __libc_init_array (C++ ctors), main().
 *   4. Weak default exception handlers.
 ******************************************************************************/

.syntax unified
.thumb

/* ---- 1) PICOBIN image definition block --------------------------------- */
.section .embedded_block, "a"
.p2align 2
embedded_block:
    .word 0xffffded3                              /* MARKER_START */
    .byte 0x42                                    /* IMAGE_TYPE item */
    .byte 0x1
    .hword 0x1021                                 /* EXE | S | ARM | RP2350 */
    .byte 0xff
    .hword (embedded_block_end - embedded_block - 16) / 4
    .byte 0
    .word embedded_end_block - embedded_block
    .word 0xab123579                              /* MARKER_END */
embedded_block_end:

.section .embedded_end_block, "a"
.p2align 2
embedded_end_block:
    .word 0xffffded3
    .byte 0xfe
    .byte 0x1
    .hword 0
    .byte 0xff
    .hword (embedded_end_block_end - embedded_end_block - 16) / 4
    .byte 0
    .word embedded_block - embedded_end_block
    .word 0xab123579
embedded_end_block_end:

/* ---- 2) ELF entry point (debugger) ------------------------------------- */
.section .reset, "ax"
.syntax unified
.thumb

#if defined(PICO2_RAM_EXEC)
/* Flash-resident boot stub (RAM-exec builds only): the bootrom's XIP mode on
 * this RP2350B board is marginal, so the app image is copied to RAM with a
 * verify-and-retry loop and then run entirely from RAM (no flash fetches after
 * boot, no marginality). The stub runs from flash in the bootrom's mode. */
.section .stub_code, "ax"
.thumb_func
.global stub_reset
.type stub_reset, %function
stub_reset:
    movs r0, #1
    msr  primask, r0               /* mask IRQs during the copy */
    ldr  r4, =__image_lma_start     /* flash source */
    ldr  r5, =__image_lma_end
    ldr  r6, =__image_vma_start     /* RAM destination */
    movs r8, #32                    /* retry budget */
stub_retry:
    mov  r0, r6
    mov  r1, r4
stub_copy:
    cmp  r1, r5
    bhs  stub_verify
    ldr  r3, [r1], #4
    str  r3, [r0], #4
    b    stub_copy
stub_verify:
    mov  r0, r6
    mov  r1, r4
stub_vloop:
    cmp  r1, r5
    bhs  stub_done
    ldr  r3, [r1], #4
    ldr  r7, [r0], #4
    cmp  r3, r7
    bne  stub_retry_bad
    b    stub_vloop
stub_retry_bad:
    subs r8, r8, #1
    bne  stub_retry
stub_done:
    movs r0, #0
    msr  primask, r0               /* re-enable IRQs (kernel manages later) */
    ldr  r0, =_reset_handler       /* jump to the RAM reset handler */
    orr  r0, r0, #1
    bx   r0
#endif

.section .reset, "ax"
.thumb_func
.global _entry_point
.type _entry_point, %function
_entry_point:
    ldr r0, =0x00000000           /* bootrom vector table base */
    ldr r1, =0xe000ed08           /* VTOR */
    str r0, [r1]
    ldmia r0!, {r1, r2}           /* r1 = initial MSP, r2 = bootrom reset */
    msr msp, r1
    bx r2

/* ---- 3) Reset handler (core 0 only) ------------------------------------ */
.thumb_func
.global _reset_handler
.type _reset_handler, %function
_reset_handler:
    /* Core 1 (if it ever reaches here) is parked back in the bootrom. */
    ldr r0, =0xd0000000           /* SIO_CPUID */
    ldr r0, [r0]
    cbz r0, 1f
    ldr r0, =0x00000000
    ldr r1, =0xe000ed08
    str r0, [r1]
    ldmia r0!, {r1, r2}
    msr msp, r1
    bx r2
1:
    /* VTOR -> our flash vector table */
    ldr r0, =__vector_table
    ldr r1, =0xe000ed08
    str r0, [r1]
    dsb
    isb

    /* Enable the single-precision FPU (CP10/CP11 full access) plus CP7, the
     * RP2350 Redundancy Coprocessor (RCP). The bootrom uses RCP instructions
     * for control-flow integrity inside its flash ROM functions; with CP7
     * access denied they fault UsageFault/NOCP. The bootrom already seeds the
     * RCP for core 0 during boot, so enabling access (CP7 bits [15:14]) is
     * enough here — no re-seed needed. */
    ldr r0, =0xe000ed88           /* CPACR */
    ldr r1, =0x00f0c000           /* CP10/CP11 (FPU) + CP7 (RCP) */
    str r1, [r0]
    dsb
    isb

    /* Disable automatic + lazy FP state preservation on exception entry
     * (FPCCR.ASPEN=bit31, LSPEN=bit30). With these on (the RP2350 reset
     * default) the M33 sets FPCA and includes FP context in the exception
     * frame, which desynchronises the port's conditional FP save/restore and
     * corrupts the context switch. Off => FP is never in the frame. */
    ldr r0, =0xe000ef34           /* FPCCR */
    ldr r1, [r0]
    bic r1, r1, #0xc0000000       /* clear ASPEN | LSPEN */
    str r1, [r0]
    /* Also clear CONTROL.FPCA so no FP context is considered active. */
    mrs r1, control
    bic r1, r1, #4                /* CONTROL.FPCA */
    msr control, r1
    dsb
    isb

    ldr sp, =__StackTop

    /* ARMv8-M: clear the stack-limit registers. The RP2350 bootrom leaves
     * MSPLIM/PSPLIM set; the µOS++ Cortex-M (v7E-M) port never touches them,
     * so when it switches to a thread's PSP (below the bootrom limit) the M33
     * faults. Clearing them makes the v7E-M context switch valid on M33. */
    movs r0, #0
    msr  msplim, r0
    msr  psplim, r0

#if defined(PICO2_PSRAM) || defined(PICO2_PSRAM_COPY)
    /* PSRAM (XIP window 1, CS1 = GPIO0): the QMI direct-mode init cannot run
     * from flash XIP, so copy the .psram_ram routine to internal SRAM. With
     * PICO2_PSRAM (data in PSRAM) it is also called here, before the .data
     * copy / .bss zeroing that target the PSRAM window; with PICO2_PSRAM_COPY
     * (diagnostic build) the app calls it itself. */
    ldr  r0, =__psram_ram_start     /* LMA (flash) */
    ldr  r1, =__psram_ram_vma       /* VMA (SRAM)   */
    ldr  r2, =__psram_ram_end
9:  cmp  r0, r2
    bhs  8f
    ldr  r3, [r0], #4
    str  r3, [r1], #4
    b    9b
8:
#if defined(PICO2_PSRAM)
    ldr  r0, =__psram_ram_vma
    orr  r0, r0, #1                 /* Thumb bit */
    blx  r0
    dsb
    isb
#endif
#endif

#if !defined(PICO2_RAM_EXEC)
    /* Copy .data from flash (__etext) into RAM */
    ldr r0, =__etext
    ldr r1, =__data_start__
    ldr r2, =__data_end__
    b 3f
2:  ldr r4, [r0], #4
    str r4, [r1], #4
3:  cmp r1, r2
    blo 2b
#endif

    /* Zero .bss */
    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    movs r2, #0
    b 5f
4:  str r2, [r0], #4
5:  cmp r0, r1
    blo 4b

    /* Run C++ static constructors (.preinit_array + .init_array) */
    bl __libc_init_array

    /* Enter the application / kernel main() with argc=0, argv=NULL (no
     * semihosting args). The kernel's _main_trampoline dumps args; garbage
     * r0/r1 here made it walk a bogus argv[] and bus-fault in printf. */
    movs r0, #0
    movs r1, #0
    bl main

6:  b 6b

/* ---- 4) Weak default handlers ------------------------------------------ */
.macro default_handler name
    .thumb_func
    .global \name
    .weak \name
    .type \name, %function
\name:
    b \name
.endm

default_handler NMI_Handler
default_handler HardFault_Handler
default_handler MemManage_Handler
default_handler BusFault_Handler
default_handler UsageFault_Handler
default_handler SVC_Handler
default_handler DebugMon_Handler
default_handler PendSV_Handler
default_handler SysTick_Handler
default_handler SIO_IRQ_FIFO_Handler
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
.global TIMER0_IRQ_2_Handler
.weak TIMER0_IRQ_2_Handler
.type TIMER0_IRQ_2_Handler, %function
.thumb_set TIMER0_IRQ_2_Handler, default_irq_handler

.thumb_func
.global TIMER0_IRQ_3_Handler
.weak TIMER0_IRQ_3_Handler
.type TIMER0_IRQ_3_Handler, %function
.thumb_set TIMER0_IRQ_3_Handler, default_irq_handler
