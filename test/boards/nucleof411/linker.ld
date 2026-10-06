/*
 * Linker script for the ST Nucleo-F411RE (STM32F411RE, Cortex-M4F).
 *
 * FLASH 512 KB @ 0x08000000, SRAM 128 KB @ 0x20000000.
 *
 * Provides the symbol set expected by the shared micro-os-plus-iii Cortex-M
 * startup (src/startup/startup.cpp + initialize-hardware.c): __vectors_start,
 * _sidata/_sdata/_edata, __bss_start__/__bss_end__, the C++ init/fini arrays,
 * and _Heap_Begin/_Heap_Limit for the vendored _sbrk().
 *
 * Part of micro-os-plus-iii-cortexm-nucleof411 — see ../PLAN.md.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Top of stack = end of RAM (loaded into vector[0] by hardware). */
_estack = ORIGIN(RAM) + LENGTH(RAM);
/* Alias expected by the shared kernel Reset_Handler / stack-fill loop. */
__stack = _estack;

/* Minimum sizes reserved for the main stack and the heap. */
_Minimum_Stack_Size = 4K;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    __vectors_start = ABSOLUTE(.);
    _interrupt_vectors = ABSOLUTE(.);   /* alias expected by kernel Reset_Handler */
    KEEP(*(.isr_vector))     /* the vector table from vectors_stm32f411xe.c */
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.after_vectors .after_vectors.*)   /* startup / handlers placed right after vectors */
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.glue_7) *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
  } >FLASH

  /* C++ exception unwinding tables. */
  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } >FLASH

  /* C++ static constructor / destructor arrays. */
  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* Load address of the .data initialisers (in FLASH). */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;              /* start of initialised data (in RAM) */
    *(.data .data.*)
    *(.RamFunc .RamFunc.*)
    . = ALIGN(4);
    _edata = .;              /* end of initialised data */
  } >RAM AT>FLASH

  .bss :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
  } >RAM

  /* Heap grows up from the end of .bss; _sbrk() enforces _Heap_Limit. */
  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    _Heap_Begin = .;
    . = . + (LENGTH(RAM) - (. - ORIGIN(RAM)) - _Minimum_Stack_Size);
    . = ALIGN(8);
    _Heap_Limit = .;
  } >RAM

  /* Fail the link if the reserved stack does not fit. */
  ._stack_guard (NOLOAD) :
  {
    . = ALIGN(4);
    . = . + _Minimum_Stack_Size;
    . = ALIGN(4);
  } >RAM

  /* newlib compatibility. */
  PROVIDE(end = __bss_end__);
  PROVIDE(_end = __bss_end__);

  /DISCARD/ :
  {
    *(.ARM.attributes)
  }
}
