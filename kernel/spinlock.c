#include "types.h"
#include "riscv.h"
#include "spinlock.h"
static int nesting;
static int restore_interrupts;
void initlock(struct spinlock *lk, char *name) { lk->locked = 0; lk->name = name; }
void push_off(void) { int enabled = intr_get(); intr_off(); if(nesting++ == 0) restore_interrupts = enabled; }
void pop_off(void) { if(--nesting == 0 && restore_interrupts) intr_on(); }
int holding(struct spinlock *lk) { return lk->locked != 0; }
void acquire(struct spinlock *lk) { push_off(); while(__atomic_exchange_n(&lk->locked, 1, __ATOMIC_ACQUIRE)) {} }
void release(struct spinlock *lk) { __atomic_store_n(&lk->locked, 0, __ATOMIC_RELEASE); pop_off(); }
