// spinlock.c - Simple spinlock that preserves the caller interrupt state.
//
// Lab 3 runs on the single boot hart, so the interrupt-save/restore context
// may be tracked in a single static variable. spinlock acquisition disables
// supervisor interrupts inside the critical section and restores the caller's
// interrupt state on release.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"

// Saved supervisor interrupt-enable state while interrupts are disabled for
// a critical section (single boot hart, Lab 3).
static uint64 saved_sie;
static int noff;

void
initlock(struct spinlock *lk, const char *name)
{
  lk->name = name;
  lk->locked = 0;
}

static void
push_off(void)
{
  uint64 x = r_sstatus();
  if (noff == 0)
    saved_sie = x & SSTATUS_SIE;
  noff += 1;
  w_sstatus(x & ~SSTATUS_SIE);
}

static void
pop_off(void)
{
  noff -= 1;
  if (noff == 0) {
    uint64 x = r_sstatus();
    if (saved_sie)
      x |= SSTATUS_SIE;
    else
      x &= ~SSTATUS_SIE;
    w_sstatus(x);
  }
}

int
holding(struct spinlock *lk)
{
  // Single boot hart (Lab 3): a held lock is simply one whose bit is set.
  return lk->locked;
}

void
acquire(struct spinlock *lk)
{
  push_off();
  if (holding(lk))
    panic("acquire");
  while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;
  __sync_synchronize();
}

void
release(struct spinlock *lk)
{
  if (!holding(lk))
    panic("release");
  __sync_synchronize();
  __sync_lock_release(&lk->locked);
  pop_off();
}
