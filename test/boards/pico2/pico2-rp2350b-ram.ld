/******************************************************************************
 * pico2-rp2350b-ram.ld - Linker (RP2350B, 16 MB flash, RAM-exec) script for
 * the Raspberry Pi Pico 2 (RP2350, M33).
 *
 * RAM-exec layout: the RP2350B boards' 16 MB flash is marginal under the
 * bootrom's XIP mode, so the entire app image (.text/.rodata/.data) is linked
 * in RAM and LOADED from flash by a tiny flash-resident stub (src/boot.S
 * stub_reset) that copies with verify-and-retry, then jumps to the RAM
 * _reset_handler. After boot no instruction fetches hit flash at all.
 *
 * Memory: 16 MB XIP flash @0x10000000, 520 KB SRAM @0x20000000. The bootrom
 * requires the vector table at flash offset 0 and a PICOBIN block in the
 * first 4 KB (see src/boot.S) — both live in the .stub FLASH section.
 ******************************************************************************/

ENTRY(_entry_point)

MEMORY {
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 16M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
}

SECTIONS {
    /* Flash-resident stub: vector table + PICOBIN block + copy/verify/retry
     * boot code. Everything here stays in flash; the app image follows. */
    .stub : ALIGN(4) {
        KEEP(*(.vectors))
        KEEP(*(.embedded_block))
        *(.stub_code)
    } > FLASH

    /* The whole app image: linked at RAM, loaded from flash after the stub.
     * __image_lma_* = flash range, __image_vma_* = RAM range (one contiguous
     * block each, so the stub copies it with a single loop). */
    .image : ALIGN(4) {
        __image_vma_start = .;
        *(.reset)
        *(.after_vectors)
        *(.text*)
        /* Executable code that must not be stalled by flash XIP fetch
         * contention (glue/ws2812.cpp's .sram_text). The whole image is
         * copied to RAM by the stub, so this is loaded with it. */
        . = ALIGN(4);
        *(.sram_text)
        *(.sram_text*)
        . = ALIGN(4);
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
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
        __data_start__ = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        __data_end__ = .;
        __image_vma_end = .;
    } > RAM AT> FLASH
    __etext = LOADADDR(.image) + (__data_start__ - __image_vma_start);
    __image_lma_start = LOADADDR(.image);
    __image_lma_end = LOADADDR(.image) + (__image_vma_end - __image_vma_start);

    .bss : ALIGN(4) {
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        *(.sbss*)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* Fault record retained across a warm reset (glue/rtos-glue.cpp places it
     * in .noinit). NOLOAD and outside the stub's copy and the .bss zeroing, so
     * it survives a warm reset. */
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
