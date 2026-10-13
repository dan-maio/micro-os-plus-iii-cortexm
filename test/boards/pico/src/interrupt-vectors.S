/******************************************************************************
 * interrupt-vectors.S - Cortex-M0+ Vector Table for the RP2040 (Pico)
 *
 * The vector table follows boot2 in flash, at 0x10000100, where the RP2040's
 * second stage enters the image (see boot.S). 16 system exceptions and the
 * RP2040's 26 external IRQs: 42 words, so the table is 256-byte aligned
 * (VTOR needs the next power of two above its size).
 ******************************************************************************/

.syntax unified
.cpu cortex-m0plus
.thumb

.section .vectors, "ax"

.p2align 8          /* 2^8 = 256 bytes */

.global __vector_table
.type __vector_table, %object

__vector_table:
    .word __StackTop
    .word _reset_handler
    .word NMI_Handler
    .word HardFault_Handler
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word SVC_Handler
    .word 0                         /* Reserved */
    .word 0                         /* Reserved */
    .word PendSV_Handler            /* exception 14, vector offset 0x38 */
    .word SysTick_Handler           /* exception 15, vector offset 0x3C */

    /* RP2040 external IRQs 0..25 begin at exception 16 (offset 0x40).
     * IRQ 15/16 are the per-core SIO inter-core FIFO IRQs, used as the SMP
     * cross-core IPI; IRQ 20 is UART0, with a WEAK handler so a test can
     * install its own (interrupt-driven TX); IRQ 5 is the USB controller.
     *
     * IRQ 0 goes to WDT_IRQHandler: the Arm CMSIS-RTOS validator's
     * TC_ThreadInterrupts takes the first external IRQ, and the validator
     * defines the handler under that name (ARM_CMSIS_VALIDATOR_IRQHandler in
     * the platform header). Weak, so every other test keeps the stub. */
    .set irq_index, 0
    .rept 26
    .if irq_index == 0
    .word WDT_IRQHandler
    .elseif irq_index == 2
    .word TIMER_IRQ_2_Handler
    .elseif irq_index == 3
    .word TIMER_IRQ_3_Handler
    .elseif irq_index == 5
    .word USBCTRL_IRQ_Handler
    .elseif irq_index == 15
    .word SIO_IRQ_PROC0_Handler
    .elseif irq_index == 16
    .word SIO_IRQ_PROC1_Handler
    .elseif irq_index == 20
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
