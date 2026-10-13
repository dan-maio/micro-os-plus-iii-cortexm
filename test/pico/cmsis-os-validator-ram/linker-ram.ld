/******************************************************************************
 * linker-ram.ld - Pure RAM execution linker script for the Raspberry Pi Pico
 * (RP2040, 2x Cortex-M0+).
 *
 * Runs entirely from internal SRAM (0x20000000, 264 KB: the four striped
 * banks and SCRATCH X/Y, contiguous). Loaded directly into SRAM by OpenOCD
 * via load_image, without writing to flash; the image starts at its own
 * vector table, so boot2 (the flash second stage) is not part of it.
 ******************************************************************************/

ENTRY(_entry_point)

MEMORY {
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

__stack_size = 0x2000;             /* 8 KB main (interrupt) stack */

SECTIONS {
    /DISCARD/ : {
        *(.boot2)
    }

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
        . = ALIGN(4);
        KEEP(*(.gcc_except_table* .gnu.linkonce.gcc_except_table.*))
        . = ALIGN(4);
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > RAM

    /* C++ static constructor / destructor arrays. */
    .preinit_array : ALIGN(4) {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > RAM

    .init_array : ALIGN(4) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > RAM

    .fini_array : ALIGN(4) {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > RAM

    .data : ALIGN(4) {
        __data_start__ = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        *(.sram_text)
        *(.sram_text*)
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM
    __etext = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        *(.sbss*)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

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
