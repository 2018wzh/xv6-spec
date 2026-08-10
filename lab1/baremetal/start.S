// Lab 1 CTF warm-up: RISC-V reset/start entry for the freestanding guest.
// QEMU loads this ELF at its linked address and jumps to _start in machine mode.

.section .text.init,"ax",@progbits
.globl _start
_start:
    // Establish the machine-mode stack from the linker-provided symbol.
    la   sp, _stack_top
    call main
hang:
    wfi
    j hang
