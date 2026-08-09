  # entry.S - the linked machine-mode entry point. kernel.ld places this
  # at 0x80000000 where qemu's -kernel jumps. QEMU enters here in machine
  # mode with interrupts disabled and a single boot hart (Lab 2).
  .section .text
  .globl _entry
_entry:
  # set up the early boot stack (one stack for the single boot hart).
  la sp, bootstacktop
  # jump to start() in machine mode, which configures the PMP entry and
  # mret's into supervisor mode at main().
  call start
spin:
  j spin