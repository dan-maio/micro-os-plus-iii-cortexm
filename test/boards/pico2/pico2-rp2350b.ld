/******************************************************************************
 * pico2-rp2350b.ld - Linker (RP2350B, 16 MB flash) script for the Raspberry Pi Pico 2 (RP2350, M33).
 *
 * C++ build: adds the .preinit_array/.init_array/.fini_array sections so the
 * reset handler (boot.S → __libc_init_array) runs static constructors, plus a
 * heap for new/malloc. Adapted from the Eclipse C reference (reference/linker.ld).
 *
 * RP2350: 16 MB XIP flash @0x10000000, 520 KB SRAM @0x20000000. The bootrom
 * requires the vector table at flash offset 0 and a PICOBIN block in the first
 * 4 KB (see src/boot.S).
 ******************************************************************************/

ENTRY(_entry_point)

MEMORY {
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 16M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
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
        /* Executable code that must run from SRAM: the WS2812 bit-banging
         * routine (glue/ws2812.cpp places it in .sram_text). Inside .data it
         * is copied to RAM by boot.S and executed there, instead of being
         * orphaned into flash XIP, where the straight-line NOP timing needs
         * to be immune to fetch contention. */
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

    /* Fault record retained across a warm reset (glue/rtos-glue.cpp places it
     * in .noinit). NOLOAD and outside __bss_start__..__bss_end__, so boot.S
     * neither copies nor zeroes it, and its address is stable across a warm
     * reset. */
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
        . += 0x20000;              /* 128 KB heap */
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

    .flash_end : ALIGN(4) {
        KEEP(*(.embedded_end_block))
    } > FLASH

    ASSERT(__data_end__ <= __bss_start__, "RAM regions overlap")
}
