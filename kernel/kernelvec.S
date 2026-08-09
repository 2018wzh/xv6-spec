# kernelvec.S - supervisor trap vector.
  #
  # kernelvec saves a complete interrupted register frame in symmetric
  # stack slots, calls the C dispatcher kerneltrap(), and on return restores
  # every register it changed before returning to the interrupted
  # supervisor context with sret. The frame layout must be fully symmetric:
  # each register that is saved is restored from the same slot, and the
  # frame is unwound to reclaim the reserved space.
  #
  # kernel.ld maps this into kernel text, so kernelvec lives in the
  # executable-but-not-writable kernel text region.
  .section .text
  .globl kernelvec
kernelvec:
  # reserve space for the full 32-register frame (32 entries * 8 bytes).
  addi sp, sp, -256

  # save every register in a fixed slot. x0 is always 0 and is not saved.
  sd ra, 0(sp)     # return address
  sd sp, 8(sp)     # original stack pointer (saved before the frame)
  sd gp, 16(sp)    # global pointer
  sd tp, 24(sp)    # thread pointer
  sd t0, 32(sp)
  sd t1, 40(sp)
  sd t2, 48(sp)
  sd s0, 56(sp)
  sd s1, 64(sp)
  sd a0, 72(sp)
  sd a1, 80(sp)
  sd a2, 88(sp)
  sd a3, 96(sp)
  sd a4, 104(sp)
  sd a5, 112(sp)
  sd a6, 120(sp)
  sd a7, 128(sp)
  sd s2, 136(sp)
  sd s3, 144(sp)
  sd s4, 152(sp)
  sd s5, 160(sp)
  sd s6, 168(sp)
  sd s7, 176(sp)
  sd s8, 184(sp)
  sd s9, 192(sp)
  sd s10, 200(sp)
  sd s11, 208(sp)
  sd t3, 216(sp)
  sd t4, 224(sp)
  sd t5, 232(sp)
  sd t6, 240(sp)

  # pass the trap frame pointer to the C dispatcher.
  mv a0, sp

  # call the C kernel trap handler; kerneltrap returns here on success.
  call kerneltrap

  # restore every register from the slot it was saved into.
  ld ra, 0(sp)
  ld sp, 8(sp)
  ld gp, 16(sp)
  ld tp, 24(sp)
  ld t0, 32(sp)
  ld t1, 40(sp)
  ld t2, 48(sp)
  ld s0, 56(sp)
  ld s1, 64(sp)
  ld a0, 72(sp)
  ld a1, 80(sp)
  ld a2, 88(sp)
  ld a3, 96(sp)
  ld a4, 104(sp)
  ld a5, 112(sp)
  ld a6, 120(sp)
  ld a7, 128(sp)
  ld s2, 136(sp)
  ld s3, 144(sp)
  ld s4, 152(sp)
  ld s5, 160(sp)
  ld s6, 168(sp)
  ld s7, 176(sp)
  ld s8, 184(sp)
  ld s9, 192(sp)
  ld s10, 200(sp)
  ld s11, 208(sp)
  ld t3, 216(sp)
  ld t4, 224(sp)
  ld t5, 232(sp)
  ld t6, 240(sp)

  # reclaim the frame and return to the interrupted supervisor context.
  addi sp, sp, 256
  sret