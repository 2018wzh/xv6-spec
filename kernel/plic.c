// plic.c - RISC-V Platform-Level Interrupt Controller (PLIC) driver for
// the QEMU virt board. In Lab 4 the lone boot hart enables the UART receive
// IRQ (IRQ 10) for its S-mode context, then claims and completes recognized
// external interrupts exactly once.
//
// PLIC register layout (per the RISC-V PLIC spec as exposed by qemu -machine
// virt):
//   priority   : PLIC + 0x000000 (4 bytes per interrupt source), source i at
//                (PLIC_PRIORITY + (i-1)*4)
//   pending    : PLIC + 0x001000
//   enable     : PLIC + 0x002000 (M-mode), PLIC + 0x002080 (S-mode),
//                one bit per context (hart)
//   threshold  : PLIC + 0x200000 (M-mode), PLIC + 0x201000 (S-mode)
//   claim/compl: PLIC + 0x200004 (M-mode), PLIC + 0x201004 (S-mode), per context

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

#define PLIC_PRIORITY   (PLIC + 0x0)       // interrupt source priorities
#define PLIC_PENDING    (PLIC + 0x1000)    // pending bits
#define PLIC_SENABLE    (PLIC + 0x2080)    // S-mode enable, one word per hart
#define PLIC_SPRIORITY  (PLIC + 0x201000)  // S-mode threshold, one word per hart
#define PLIC_SCLAIM     (PLIC + 0x201004)  // S-mode claim/complete, per hart

#define PLIC_PRIORITY_INIT 1

// Set the UART interrupt priority high enough to be delivered. Runs on the
// boot hart during trap-stage initialization, before supervisor interrupts
// are enabled. The spec interface declares this operation as `plicinit`, and
// it must configure UART priority before the per-hart enable mask is set.
void
plicinit(void)
{
  // Set desired IRQ priorities (UART IRQ 10 and virtio block IRQ 1).
  *(uint32 *)(PLIC_PRIORITY + (UART0_IRQ - 1) * 4) = PLIC_PRIORITY_INIT;
  *(uint32 *)(PLIC_PRIORITY + (VIRTIO0_IRQ - 1) * 4) = PLIC_PRIORITY_INIT;
}

// Enable the UART IRQ for the current hart's S-mode context and set the
// priority threshold so any enabled interrupt reaches the hart. Runs on the
// boot hart; called before enabling supervisor interrupts.
void
plicinithart(void)
{
  int hart = r_mhartid();

  /* enable UART IRQ 10 and virtio block IRQ 1 for this hart's S-mode
     context (bit index = IRQ). */
  *(uint32 *)(PLIC_SENABLE + hart * 4) |= (1U << UART0_IRQ);
  *(uint32 *)(PLIC_SENABLE + hart * 4) |= (1U << VIRTIO0_IRQ);

  /* threshold 0: deliver every pending, enabled, prioritized interrupt. */
  *(uint32 *)(PLIC_SPRIORITY + hart * 4) = 0;
}

// Claim the highest-priority pending interrupt for the current hart, or
// return 0 when no interrupt is pending. Called only from S-mode
// external-interrupt dispatch.
int
plic_claim(void)
{
  int hart = r_mhartid();
  int irq = *(uint32 *)(PLIC_SCLAIM + hart * 4);
  return irq;
}

// Complete the previously claimed interrupt so the PLIC clears its pending
// bit. Writes the identical nonzero IRQ back to the claim/complete register.
void
plic_complete(int irq)
{
  int hart = r_mhartid();
  *(uint32 *)(PLIC_SCLAIM + hart * 4) = irq;
}