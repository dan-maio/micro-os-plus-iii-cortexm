/******************************************************************************
 * psram-exec.ld — RP2350B "run code from PSRAM" test (Pi-Zero).
 *
 * Layout:
 *   FLASH  0x10000000  .stub (vectors+PICOBIN+copy stub) and the LOAD
 *                      addresses of .image (the SRAM monitor) and
 *                      .psram_code (the PSRAM-resident procedure).
 *   SRAM   0x20000000  .image (the whole monitor: clocks/uart/psram::init/
 *                      main) — RAM-exec, copied at boot by stub_reset. After
 *                      boot no flash instruction fetches happen.
 *   PSRAM  0x11000000  .psram_code (VMA) — the procedure that RUNS FROM
 *                      PSRAM; the monitor copies it here after psram::init().
 *                      .psram — a tiny float matrix[10], filled by the app.
 *
 * The monitor (SRAM ~0x2000xxxx) calls the PSRAM procedure (~0x1100xxxx):
 * SRAM and PSRAM are ~4 GB apart, far beyond the BL range, so the call goes
 * through a function pointer (BLX to a register = a native long call) — no
 * veneer is needed. Inside the procedure, the only external call is the
 * passed-in putc() function pointer (also a long call). Everything else the
 * procedure touches is PSRAM (the matrix) and its own stack (the monitor's).
 ******************************************************************************/

ENTRY(_entry_point)

MEMORY {
    FLASH  (rx)  : ORIGIN = 0x10000000, LENGTH = 16M
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
    PSRAM_A (rwx): ORIGIN = 0x11000000, LENGTH = 4M   /* app A */
    PSRAM_B (rwx): ORIGIN = 0x11400000, LENGTH = 4M   /* app B */
}

SECTIONS {
    /* Flash-resident stub: vector table + PICOBIN + copy/verify/retry boot. */
    .stub : ALIGN(4) {
        KEEP(*(.vectors))
        KEEP(*(.embedded_block))
        *(.stub_code)
    } > FLASH

    /* The whole SRAM monitor: linked in RAM, loaded from flash by the stub. */
    .image : ALIGN(4) {
        __image_vma_start = .;
        *(.reset)
        *(.after_vectors)
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        *(.psram_ram)
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

    /* Two self-contained PSRAM APP images, A and B, at separate addresses. Each
     * has its own vector table first (VTOR target), then code/rodata/data; VMA
     * in PSRAM, LMA in flash. The supervisor copies each from its flash LMA into
     * PSRAM and switches VTOR + calls each app's entry (vector[1]). ALIGN(256):
     * the M33 VTOR must be aligned to the table size rounded up to a power of
     * two (>=128); 256 is safe for 16 entries. */
    .psram_a : ALIGN(256) {
        __psram_a_vma_start = .;
        KEEP(*(.psram_a_vectors))
        *(.psram_a_text*)
        *(.psram_a_rodata*)
        . = ALIGN(4);
        *(.psram_a_data*)
        . = ALIGN(4);
        __psram_a_vma_end = .;
    } > PSRAM_A AT> FLASH
    __psram_a_lma_start = LOADADDR(.psram_a);
    .psram_a_bss (NOLOAD) : ALIGN(4) {
        __psram_a_bss_start = .;
        *(.psram_a_bss*)
        . = ALIGN(4);
        __psram_a_bss_end = .;
    } > PSRAM_A

    .psram_b : ALIGN(256) {
        __psram_b_vma_start = .;
        KEEP(*(.psram_b_vectors))
        *(.psram_b_text*)
        *(.psram_b_rodata*)
        . = ALIGN(4);
        *(.psram_b_data*)
        . = ALIGN(4);
        __psram_b_vma_end = .;
    } > PSRAM_B AT> FLASH
    __psram_b_lma_start = LOADADDR(.psram_b);
    .psram_b_bss (NOLOAD) : ALIGN(4) {
        __psram_b_bss_start = .;
        *(.psram_b_bss*)
        . = ALIGN(4);
        __psram_b_bss_end = .;
    } > PSRAM_B

    .heap (NOLOAD) : ALIGN(8) {
        __end__ = .;
        end = __end__;
        PROVIDE(_end = __end__);
        __heap_start = .;
        _Heap_Begin = .;
        . += 0x4000;              /* 16 KB heap (newlib) */
        __heap_limit = .;
        _Heap_Limit = .;
        __heap_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(8) {
        . += 0x4000;              /* 16 KB main stack */
        __StackTop = .;
        __stack = __StackTop;
        __StackLimit = __StackTop - 0x4000;
    } > RAM

    .flash_end : ALIGN(4) {
        KEEP(*(.embedded_end_block))
    } > FLASH

    ASSERT(__data_end__ <= __bss_start__, "RAM regions overlap")
}
