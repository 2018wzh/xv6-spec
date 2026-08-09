// start.c - machine-mode entry. Runs in machine mode on the boot stack,
// configures a single unlocked NAPOT PMP entry, then mret's into
// supervisor mode at main().

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// entry.S jumps here in machine mode with interrupts disabled.
void
start(void)
{
  unsigned long x;

  // Configure PMP entry 0: grant supervisor mode read/write/execute
  // access to the maximal NAPOT region covering all of physical memory,
  // so that after mret the linked kernel image and the UART MMIO range
  // at 0x10000000 remain accessible. The entry is left unlocked (PMP
  // locking is outside Lab 2 scope).
  w_pmpaddr0(0x3fffffffffffffL);
  w_pmpcfg0(PMP_R | PMP_W | PMP_X | PMP_A_NAPOT);

  // set M Previous Privilege mode to Supervisor, for mret.
  x = r_mstatus();
  x &= ~MSTATUS_MPP;       // clear MPP
  x |= MSTATUS_MPP_S;      // set MPP to supervisor
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  w_mepc((uint64)main);

  // switch to supervisor mode and jump to main().
  mret();
}