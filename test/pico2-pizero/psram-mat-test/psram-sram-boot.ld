/******************************************************************************
 * psram-sram-boot.ld — RP2350B: whole C runtime in SRAM, ONLY the big solver
 * arrays in PSRAM.
 *
 * The console + kernel never touch PSRAM (all .data/.bss/heap/stack in SRAM),
 * so boot is a normal SRAM build — no boot-clock PSRAM init (which this board's
 * GPIO47 PSRAM does NOT survive). The QMI init routine (.psram_ram) is copied
 * to SRAM by boot.S (-DPICO2_PSRAM_COPY) but NOT called at boot; a constructor
 * calls psram::init() at 150 MHz. The big arrays (A, A_block, LU) go in the
 * .psram section (0x11000000, NOLOAD; filled by the app after init).
 ******************************************************************************/

ENTRY(_entry_point)

MEMORY {
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 16M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
    PSRAM (rwx) : ORIGIN = 0x11000000, LENGTH = 8M
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

    .psram_ram : ALIGN(4) {
        __psram_ram_vma = .;
        KEEP(*(.psram_ram))
        . = ALIGN(4);
        __psram_ram_vma_end = .;
    } > RAM AT> FLASH
    __psram_ram_start = LOADADDR(.psram_ram);
    __psram_ram_end = LOADADDR(.psram_ram) + (__psram_ram_vma_end - __psram_ram_vma);

    .data : ALIGN(4) {
        __data_start__ = .;
        *(.data*)
        *(.sdata*)
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

    .psram (NOLOAD) : ALIGN(8) {
        __psram_start = .;
        *(.psram*)
        . = ALIGN(8);
        __psram_end = .;
    } > PSRAM

    .heap (NOLOAD) : ALIGN(8) {
        __end__ = .;
        end = __end__;
        PROVIDE(_end = __end__);
        __heap_start = .;
        _Heap_Begin = .;
        . += 0x20000;
        __heap_limit = .;
        _Heap_Limit = .;
        __heap_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(8) {
        . += 0x8000;
        __StackTop = .;
        __stack = __StackTop;
        __StackLimit = __StackTop - 0x8000;
    } > RAM

    .flash_end : ALIGN(4) {
        KEEP(*(.embedded_end_block))
    } > FLASH

    ASSERT(__data_end__ <= __bss_start__, "RAM regions overlap")
    ASSERT(__psram_ram_vma_end <= __StackLimit, "psram_ram overlaps stack")
}
