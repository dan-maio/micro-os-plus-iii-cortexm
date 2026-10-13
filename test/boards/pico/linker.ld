/******************************************************************************
 * linker.ld - Linker script for the Raspberry Pi Pico (RP2040, 2x M0+).
 *
 * The Pico 2 board's script (test/boards/pico2/linker.ld) for the RP2040's
 * memory and boot flow:
 *
 *   FLASH  2 MB XIP @0x10000000. The first 256 bytes are boot2 (src/boot2.S),
 *          which the bootrom loads and checks; the vector table follows at
 *          0x10000100, where boot2 enters the image.
 *   RAM    264 KB @0x20000000: the four striped 64 KB banks and the two 4 KB
 *          SCRATCH banks above them, contiguous, used as one region.
 *
 * The heap is whatever RAM .data/.bss leave below the main stack, instead of
 * the Pico 2's fixed 128 KB: the RP2040 has half the SRAM.
 ******************************************************************************/

ENTRY(_entry_point)

MEMORY {
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

__stack_size = 0x2000;             /* 8 KB main (interrupt) stack */

SECTIONS {
    .boot2 : {
        __boot2_start__ = .;
        KEEP(*(.boot2))
        __boot2_end__ = .;
    } > FLASH
    ASSERT(__boot2_end__ - __boot2_start__ == 256,
           "boot2 must be exactly 256 bytes")

    .text : ALIGN(256) {
        KEEP(*(.vectors))
        *(.reset)
        *(.after_vectors)
        *(.after_vectors*)
         *(.text*)
         *(.rodata*)
         *(.srodata*)
         KEEP(*(.init))
         KEEP(*(.fini))
         /* Exception handling: the compiler's LSDA (type tables + encodings)
          * and the ARM unwind index. Without .gcc_except_table the C++
          * personality decodes garbage and aborts on the first throw. */
         . = ALIGN(4);
         KEEP(*(.gcc_except_table* .gnu.linkonce.gcc_except_table.*))
         . = ALIGN(4);
         *(.ARM.extab* .gnu.linkonce.armextab.*)
         . = ALIGN(4);
         __exidx_start = .;
         *(.ARM.exidx* .gnu.linkonce.armexidx.*)
         __exidx_end = .;
    } > FLASH

    /* C++ static constructor / destructor arrays. */
    .preinit_array : ALIGN(4) {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array : ALIGN(4) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array : ALIGN(4) {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    .data : ALIGN(4) {
        __data_start__ = .;
        *(.data*)
        *(.sdata*)
        /* Code that must run from SRAM (.sram_text), copied by boot.S. */
        . = ALIGN(4);
        *(.sram_text)
        *(.sram_text*)
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM AT> FLASH
    __etext = LOADADDR(.data);

    .bss : ALIGN(4) {
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        *(.sbss*)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* Fault record retained across a warm reset (glue/rtos-glue.cpp). NOLOAD
     * and outside .bss, so boot.S neither copies nor zeroes it. */
    .noinit (NOLOAD) : ALIGN(4) {
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
    } > RAM

    .heap (NOLOAD) : ALIGN(8) {
        __end__ = .;
        end = __end__;
        PROVIDE(_end = __end__);
        __heap_start = .;
        _Heap_Begin = .;
        . = ORIGIN(RAM) + LENGTH(RAM) - __stack_size;
        __heap_limit = .;
        _Heap_Limit = .;
        __heap_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(8) {
        . += __stack_size;
        __StackTop = .;
        __stack = __StackTop;
        __StackLimit = __StackTop - __stack_size;
    } > RAM

    ASSERT(__data_end__ <= __bss_start__, "RAM regions overlap")
    ASSERT(_Heap_Limit - _Heap_Begin >= 0x4000,
           "less than 16 KB of RAM left for the heap")
}
