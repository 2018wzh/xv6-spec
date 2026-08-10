/* kernel/user.ld - Lab 6 user program linker script (kernel/file).
 *
 * User programs are freestanding RISC-V binaries linked at the fixed user
 * base address 0x0, with a writable data segment below the process-size
 * limit (PGSIZE-aligned). The first process loads a bounded user workload
 * at user virtual address 0 and enters it via usertrapret. This script is
 * used by the Makefile user-program rule to produce a flat binary embedded
 * into the kernel image.
 */

OUTPUT_ARCH( "riscv" )
ENTRY( _start )

SECTIONS
{
  . = 0x0;

  .text : {
    *(.text .text.*)
  }

  .rodata : {
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
  }

  .data : {
    *(.sdata .sdata.*)
    *(.data .data.*)
  }

  .bss : {
    *(.sbss .sbss.*)
    *(.bss .bss.*)
  }

  /DISCARD/ : {
    *(.eh_frame)
    *(.comment)
    *(.note .note.*)
  }
}
