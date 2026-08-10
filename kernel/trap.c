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
#include "proc.h"
#include "syscall.h"
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

// Supervisor trap cause value for a user-mode ecall.
#define SCAUSE_USER_ECALL 8

// Dispatch a trap from user mode. uservec has already saved the complete
// user register set into the current process's trap frame and switched to
// the kernel page table. A user ecall advances sepc, dispatches through
// kernel/syscall, and reaches usertrapret; an unexpected user exception
// terminates the process with evidence.
void
usertrap(void)
{
  struct proc *p = myproc();
  uint64 scause = r_scause();
  uint64 sepc = r_sepc();
  struct usertrapframe *tf = (struct usertrapframe *)p->trapframe;

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");
  w_stvec((uint64)kernelvec);  // handle kernel traps while dispatching.

  if (scause == SCAUSE_USER_ECALL) {
    // Advance sepc past the ecall before dispatch.
    tf->epc = sepc;
    sepc += 4;
    w_sepc(sepc);

    // Dispatch the validated syscall and usertrapret returns to user mode.
    syscall();
    usertrapret();
    return;
  }

  // An unexpected user-mode exception: terminate with diagnostic evidence.
  printf("usertrap: exception scause=%lx sepc=%lx stval=%lx pid=%d\n",
         scause, sepc, r_stval(), p->pid);
  panic("unexpected user exception");
}

// Return to user mode. Configures the trap frame's kernel handoff fields,
// sets stvec to uservec, clears SPP and sets SPIE so sret cannot inherit
// supervisor privilege, then jumps into the trampoline's userret, which
// switches to the user page table and restores the user register set.
void
usertrapret(void)
{
  struct proc *p = myproc();
  struct usertrapframe *tf = (struct usertrapframe *)p->trapframe;
  uint64 trampoline_userret =
    TRAMPOLINE + ((uint64)userret - (uint64)trampoline);
  uint64 user_pagetable = (uint64)p->pagetable;
  uint64 sstatus = r_sstatus();

  // Kernel must not write user memory after the page-table switch; publish
  // the kernel handoff fields while still on the kernel page table.
  tf->kernel_satp = r_satp();
  tf->kernel_sp = p->kstack + PGSIZE;
  tf->kernel_trap = (uint64)usertrap;
  tf->kernel_hartid = r_tp();

  // Prepare sstatus: SPP clear (return to user), SPIE set so the next
  // user-mode trap re-enables interrupts.
  sstatus &= ~SSTATUS_SPP;
  sstatus |= SSTATUS_SPIE;
  w_sstatus(sstatus);

  // Save the user program counter and tell uservec where the trap frame is.
  w_sepc(tf->epc);
  w_sscratch(TRAPFRAME);

  // On user traps, uservec handles user traps.
  w_stvec(TRAMPOLINE + ((uint64)uservec - (uint64)trampoline));

  // Switch to the user page table and restore user registers via userret.
  ((void (*)(uint64, uint64))trampoline_userret)(TRAPFRAME, user_pagetable);
}