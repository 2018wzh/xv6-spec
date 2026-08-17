OUTPUT_ARCH(riscv)
ENTRY(_entry)
SECTIONS {
  . = 0x80000000;
  .text : { kernel/entry.o(_entry) *(.text .text.*) }
  .rodata : { *(.srodata .srodata.*) *(.rodata .rodata.*) }
  .data : { *(.sdata .sdata.*) *(.data .data.*) }
  .bss : { *(.sbss .sbss.*) *(.bss .bss.*) }
  PROVIDE(etext = .); PROVIDE(end = .);
}
