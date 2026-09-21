/******************************************************************************
 * pico2-rp2350b-psram.ld - Linker (RP2350B, 16 MB flash + 8 MB PSRAM) for the
 * Raspberry Pi Pico 2 (RP2350, M33).
 *
 * Layout: code runs from flash XIP (window 0); the app DATA (.data/.bss/heap)
 * lives in the PSRAM (window 1, CS1 = GPIO0 on WeAct RP2350B) at 0x11000000.
 * Internal SRAM holds only the small .psram_ram init routine and the main
 * stack (the bootrom sets MSP from __StackTop before the PSRAM is usable).
 ******************************************************************************/

ENTRY(_entry_point)

MEMORY {
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 16M
    PSRAM (rwx) : ORIGIN = 0x11000000, LENGTH = 8M
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
}

SECTIONS {
    .text : ALIGN(4) {
        KEEP(*(.vectors))
        KEEP(*(.embedded_block))
        *(.reset)
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
    } > FLASH

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

    /* PSRAM init routine: linked in internal SRAM, loaded from flash. */
    .psram_ram : ALIGN(4) {
        __psram_ram_vma = .;
        KEEP(*(.psram_ram))
        . = ALIGN(4);
        __psram_ram_vma_end = .;
    } > SRAM AT> FLASH
    __psram_ram_start = LOADADDR(.psram_ram);
    __psram_ram_end = LOADADDR(.psram_ram) + (__psram_ram_vma_end - __psram_ram_vma);

    /* Writable data in PSRAM (window 1). */
    .data : ALIGN(4) {
        __data_start__ = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        __data_end__ = .;
    } > PSRAM AT> FLASH
    __etext = LOADADDR(.data);

    .bss : ALIGN(4) {
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        *(.sbss*)
        . = ALIGN(4);
        __bss_end__ = .;
    } > PSRAM

    .heap (NOLOAD) : ALIGN(8) {
        __end__ = .;
        end = __end__;
        PROVIDE(_end = __end__);
        __heap_start = .;
        _Heap_Begin = .;
        . += 0x20000;              /* 128 KB heap */
        __heap_limit = .;
        _Heap_Limit = .;
        __heap_end = .;
    } > PSRAM

    .stack (NOLOAD) : ALIGN(8) {
        . += 0x8000;               /* 32 KB main stack */
        __StackTop = .;
        __stack = __StackTop;
        __StackLimit = __StackTop - 0x8000;
    } > SRAM

    .flash_end : ALIGN(4) {
        KEEP(*(.embedded_end_block))
    } > FLASH

    ASSERT(__data_end__ <= __bss_start__, "PSRAM data regions overlap")
    ASSERT(__psram_ram_vma_end <= __StackLimit, "psram_ram overlaps stack")
}
