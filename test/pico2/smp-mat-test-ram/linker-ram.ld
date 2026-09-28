/******************************************************************************
 * linker-ram.ld - Pure RAM execution linker script for Raspberry Pi Pico 2
 * (RP2350, dual Cortex-M33 SMP).
 *
 * Runs entirely from internal SRAM (0x20000000, 520 KB).
 * Loaded directly into SRAM by OpenOCD via load_image without writing to flash.
 ******************************************************************************/

ENTRY(_entry_point)

MEMORY {
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
}

SECTIONS {
    .text : ALIGN(4) {
        __vector_table = .;
        KEEP(*(.vectors))
        KEEP(*(.embedded_block))
        KEEP(*(.embedded_end_block))
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

    .bss (NOLOAD) : ALIGN(8) {
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        *(.sbss*)
        . = ALIGN(8);
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
        . += 0x10000;              /* 64 KB heap */
        __heap_limit = .;
        _Heap_Limit = .;
        __heap_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(8) {
        . += 0x8000;               /* 32 KB main stack */
        __StackTop = .;
        __stack = __StackTop;
        __StackLimit = __StackTop - 0x8000;
    } > RAM

    ASSERT(__data_end__ <= __bss_start__, "RAM regions overlap")
}
