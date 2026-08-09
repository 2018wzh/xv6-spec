.section .text
.global _entry
.global _entry_secondary
_entry:
#ifdef PLATFORM_VISIONFIVE2
        # U-Boot enters the boot hart in S-mode with hartid in a0 and DTB in a1.
        # Use the boot CPU stack while the DTB is validated and logical CPU
        # indices are established.
        la sp, stack0
        li t0, 1024*4
        add sp, sp, t0
        call supervisor_start
1:
        wfi
        j 1b

_entry_secondary:
        # SBI HSM passes the physical hartid in a0 and our logical CPU index
        # as the opaque value in a1.
        la sp, stack0
        li t0, 1024*4
        addi t1, a1, 1
        mul t0, t0, t1
        add sp, sp, t0
        call supervisor_secondary_start
2:
        wfi
        j 2b
#else
        # qemu -kernel loads the kernel at 0x80000000 and enters M-mode.
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + ((hartid + 1) * 4096)
        mv t0, a0
        la sp, stack0
        li t1, 1024*4
        addi t2, t0, 1
        mul t1, t1, t2
        add sp, sp, t1
        # jump to start() in start.c
        call start
spin:
        j spin
_entry_secondary:
        j _entry
#endif
