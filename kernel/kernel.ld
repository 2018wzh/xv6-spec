OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x80000000,
   * where qemu's -kernel jumps.
   */
  . = 0x80000000;

  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    _bss = .;
    /* one early stack for the single boot hart (Lab 2). */
    bootstack = .;
    . += 4096;
    bootstacktop = .;
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    *(.bss .bss.*)
    . = ALIGN(16);
    _ebss = .;
  }

  /* trampoline on its own page for the highest Sv39 kernel virtual page. */
  . = ALIGN(0x1000);
  .trampoline : {
    *(.trampoline)
  }
  PROVIDE(trampoline = .);

  PROVIDE(end = .);
}