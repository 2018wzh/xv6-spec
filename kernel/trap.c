// trap.c - supervisor trap entry and dispatch.
//
// trapinit installs kernelvec as the supervisor trap vector before supervisor
// interrupts are enabled. kerneltrap dispatches the current supervisor trap.
// devintr claims, dispatches, and completes recognized external interrupts
// through the PLIC, and diagnoses (rather than entering external-device
// dispatch for) supervisor timer and software interrupts, all outside Lab 4.
//
// Kernel trap entering happens only for traps whose saved SPP bit identifies
// supervisor mode; the kernel never returns from an unexpected supervisor
// exception (it panics with scause/sepc/stval evidence after a bounded
// diagnostic).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

// installed by trapinit; kernelvec saves the interrupted frame in assembly.
extern void kernelvec(void);

// Supervisor external-interrupt cause value.
#define SCAUSE_SEXTERNAL (1L << 63 | 9)

// Install the supervisor trap vector. Runs while interrupts are still
// disabled, so no trap can arrive before stvec names kernelvec.
void
trapinit(void)
{
  w_stvec((uint64)kernelvec);
}

// Dispatch the current supervisor trap. kernelvec saved a complete frame in
// symmetric stack slots and sret restores it on return. A clean boot through
// vector installation and bounded interrupt dispatch returns to supervisor
// mode at the original sepc with the original sstatus state.
void
kerneltrap(struct trapframe *tf)
{
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  uint64 sepc = r_sepc();
  uint64 stval = r_stval();
  int which_dev = 0;

  // kernelvec handles only traps whose saved SPP bit identifies supervisor.
  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not supervisor");
  (void)tf;

  if (scause & (1L << 63)) {
    // An interrupt: route by cause.
    uint64 irq = scause & 0xff;
    switch (irq) {
      case 9:  // supervisor external interrupt
        which_dev = devintr();
        break;
      default:
        // supervisor timer/software interrupts are outside Lab 4 dispatch;
        // emit bounded scause/sepc/stval diagnostics instead.
        printf("kerneltrap: interrupt scause=%lx sepc=%lx stval=%lx\n",
               scause, sepc, stval);
        break;
    }
    if (which_dev == 0 && irq == 9) {
      // An external interrupt that claimed no device: diagnose and continue.
      printf("kerneltrap: unhandled external interrupt scause=%lx sepc=%lx\n",
             scause, sepc);
    }
  } else {
    // An unexpected supervisor exception: panic with diagnostic register
    // values (returning from it is forbidden).
    printf("kerneltrap: exception scause=%lx sepc=%lx stval=%lx\n",
           scause, sepc, stval);
    panic("unexpected supervisor exception");
  }
}

// Dispatch external interrupts. Returns 1 when an external interrupt was
// claimed and completed, and 0 for any cause that does not describe a
// supervisor external interrupt (without acknowledging unrelated state).
int
devintr(void)
{
  uint64 scause = r_scause();
  uint64 stval = r_stval();
  int irq;

  if (scause == SCAUSE_SEXTERNAL) {
    irq = plic_claim();
    if (irq == 0)
      return 0;  // no pending interrupt; nothing to complete.

    if (irq == UART0_IRQ) {
      uartintr();  // consume every currently available receive byte.
    } else {
      // Unknown external IRQ (e.g. a VIRTIO IRQ in a later lab): report it
      // in a bounded diagnostic, then complete without corrupting device
      // state.
      printf("devintr: unknown external IRQ %d (stval=%lx)\n", irq, stval);
    }

    plic_complete(irq);  // complete the identical claimed IRQ exactly once.
    return 1;
  }
  return 0;
}