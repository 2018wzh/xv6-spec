# trampoline.S - user trap entry (uservec) and user return (userret).
#
# The trampoline code is mapped read-execute at the identical virtual
# address (TRAMPOLINE) in both the kernel and every user page table, so it
# runs without a page-table pointer being a raw user-controlled value.
# All general registers and the kernel handoff fields cross the user/kernel
# privilege boundary only through the trap-frame ABI slots declared in
# kernel/proc.h (see interface/trap-frame).
#
# On a user trap, stvec names uservec and sscratch holds TRAPFRAME. uservec
# atomically swaps a0 and sscratch so a0 becomes the trap-frame base (the
# user's a0 is preserved in sscratch), saves the complete user register set,
# then switches to the kernel page table and jumps to usertrap.
#
# userret(TRAPFRAME, pagetable) restores the full user register set after
# switching back to the user page table, leaves sscratch equal to TRAPFRAME
# (so the next user trap can find it), and sret's into user mode with a0 set
# to the saved user a0.
  .section .trampoline,"ax"
  .globl uservec
  .globl userret

uservec:
  # a0 holds the user's a0; sscratch holds TRAPFRAME.
  # Atomically swap: a0 = TRAPFRAME, sscratch = user's a0.
  csrrw a0, sscratch, a0

  # Save every caller and callee-saved user register except a0 (still in
  # sscratch) into the trap frame at the interface/trap-frame offsets.
  sd ra, 40(a0)
  sd sp, 48(a0)
  sd gp, 56(a0)
  sd tp, 64(a0)
  sd t0, 72(a0)
  sd t1, 80(a0)
  sd t2, 88(a0)
  sd s0, 96(a0)
  sd s1, 104(a0)
  sd a1, 120(a0)
  sd a2, 128(a0)
  sd a3, 136(a0)
  sd a4, 144(a0)
  sd a5, 152(a0)
  sd a6, 160(a0)
  sd a7, 168(a0)
  sd s2, 176(a0)
  sd s3, 184(a0)
  sd s4, 192(a0)
  sd s5, 200(a0)
  sd s6, 208(a0)
  sd s7, 216(a0)
  sd s8, 224(a0)
  sd s9, 232(a0)
  sd s10, 240(a0)
  sd s11, 248(a0)
  sd t3, 256(a0)
  sd t4, 264(a0)
  sd t5, 272(a0)
  sd t6, 280(a0)

  # Save the user's a0 (still in sscratch) into trap-frame slot 112.
  csrr t0, sscratch
  sd t0, 112(a0)

  # Use the kernel handoff fields to switch to the kernel page table and
  # run usertrap on the process's kernel stack.
  ld t0, 8(a0)      # kernel_sp
  ld t1, 0(a0)      # kernel_satp
  ld t2, 16(a0)     # kernel_trap
  csrw satp, t1
  sfence.vma
  mv sp, t0
  jr t2

userret:
  # userret(TRAPFRAME, pagetable): a0 = TRAPFRAME, a1 = user page table.
  # Switch to the user page table and flush the TLB before restoring any
  # user register (kernel mappings must never be left accessible to user
  # mode).
  csrw satp, a1
  sfence.vma

  # Leave sscratch set to TRAPFRAME so the next user trap can find it.
  csrw sscratch, a0

  # Reload the saved user a0 into a scratch register (restored last).
  ld t0, 112(a0)

  # Restore every user register from its trap-frame slot.
  ld ra, 40(a0)
  ld sp, 48(a0)
  ld gp, 56(a0)
  ld tp, 64(a0)
  ld t1, 80(a0)
  ld t2, 88(a0)
  ld s0, 96(a0)
  ld s1, 104(a0)
  ld a1, 120(a0)
  ld a2, 128(a0)
  ld a3, 136(a0)
  ld a4, 144(a0)
  ld a5, 152(a0)
  ld a6, 160(a0)
  ld a7, 168(a0)
  ld s2, 176(a0)
  ld s3, 184(a0)
  ld s4, 192(a0)
  ld s5, 200(a0)
  ld s6, 208(a0)
  ld s7, 216(a0)
  ld s8, 224(a0)
  ld s9, 232(a0)
  ld s10, 240(a0)
  ld s11, 248(a0)
  ld t3, 256(a0)
  ld t4, 264(a0)
  ld t5, 272(a0)
  ld t6, 280(a0)

  # Restore the saved user a0. sscratch still holds TRAPFRAME.
  mv a0, t0
  sret