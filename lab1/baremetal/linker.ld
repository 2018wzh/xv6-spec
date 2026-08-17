OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
    ram (rwx) : ORIGIN = 0x80000000, LENGTH = 0x2000000
}

SECTIONS {
    . = 0x80000000;
    .text : ALIGN(4) {
        *(.text.init)
        *(.text*)
    } > ram
    .rodata : ALIGN(4) {
        *(.rodata*)
    } > ram
    .data : ALIGN(4) {
        *(.data*)
        *(sdata*)   /* embedded binary blob typically lands in .data/.rodata */
    } > ram
    .bss : ALIGN(4) {
        *(.bss*)
        *(COMMON)
        *(sbss*)
    } > ram
    . = ALIGN(16);
    . = . + 0x10000;   /* 64 KiB stack */
    _stack_top = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
        *(.riscv.attributes)
    }
}
