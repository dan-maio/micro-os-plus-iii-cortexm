/******************************************************************************
 * interrupt-vectors.S - Cortex-M33 Vector Table for the RP2350 (Pico 2)
 *
 * The vector table is placed at flash offset 0x0000 (address 0x10000000)
 * by the linker script, where the RP2350 bootrom expects it.
 *
 * On ARMv8-M the table has 16 system exceptions followed by the external
 * (NVIC) IRQs. The RP2350 exposes 52 external IRQs (NUM_IRQS = 52), so the
 * table must be 512-byte aligned (the linker script guarantees this).
 *
 * Layout:
 *   0x00  Initial stack pointer (__StackTop, from the linker script)
 *   0x04  Reset handler
 *   0x08  NMI
 *   0x0C  HardFault
 *   0x10  MemManage
 *   0x14  BusFault
 *   0x18  UsageFault
 *   0x1C  Reserved
 *   0x20  Reserved
 *   0x24  Reserved
 *   0x28  Reserved
 *   0x2C  SVCall
 *   0x30  DebugMonitor
 *   0x34  Reserved
 *   0x38  Reserved
 *   0x3C  PendSV
 *   0x40  SysTick
 *   0x44  External IRQ 0 .. IRQ 51
 ******************************************************************************/

.syntax unified
.thumb

.section .vectors, "ax"

.p2align 9          /* 2^9 = 512 bytes (required for >48 IRQs on v8-M) */

.global __vector_table
.type __vector_table, %object

__vector_table:
    .word __StackTop
#if defined(PICO2_RAM_EXEC)
    .word stub_reset               /* flash stub copies the image to RAM first */
#else
    .word _reset_handler
#endif
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word SVC_Handler
    .word DebugMon_Handler
    .word 0                         /* Reserved */
    .word PendSV_Handler            /* exception 14, vector offset 0x38 */
    .word SysTick_Handler           /* exception 15, vector offset 0x3C */

    /* RP2350 external IRQs 0..51 (52 IRQs) begin at exception 16 (offset
     * 0x40). IRQ 25 is the SIO inter-core FIFO IRQ, used as the SMP
     * cross-core IPI; IRQ 33 is UART0, with a WEAK handler so a test can
     * install its own (interrupt-driven TX) without touching this file; all
     * others are stubs. NOTE: there is NO extra vector between SysTick and
     * IRQ 0 — an earlier "defensive" PendSV there shifted every external-IRQ
     * handler one slot late, so IRQ 25 jumped to default_irq_handler (an
     * infinite loop) the moment it was enabled. */
    .set irq_index, 0
    .rept 52
    .if irq_index == 2
    .word TIMER0_IRQ_2_Handler
    .elseif irq_index == 3
    .word TIMER0_IRQ_3_Handler
    .elseif irq_index == 14
    .word USBCTRL_IRQ_Handler       /* IRQ 14: USB controller (usb-hid driver) */
    .elseif irq_index == 25
    .word SIO_IRQ_FIFO_Handler
    .elseif irq_index == 33
    .word UART0_IRQ_Handler
    .else
    .word default_irq_handler
    .endif
    .set irq_index, irq_index + 1
    .endr

.size __vector_table, . - __vector_table

/*
 * End of interrupt-vectors.S
 */
