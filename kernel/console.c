// console.c - kernel console. Owns the single console spinlock acquired by
// ordinary (non-trap) output, and processes received UART bytes. After the
// trap-stage handoff, console and printk own ordinary serial output.
//
// lock_order: the console lock is acquired "before no other lock" (i.e. it is
// the outermost lock). interrupt_rules: output in trap context already runs
// with supervisor interrupts disabled, so synchronous bounded UART writes are
// safe.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

static struct spinlock cons;

static void
consputc(int c)
{
  uartputc_sync(c);
}

void
consoleinit(void)
{
  initlock(&cons, "cons");
}

// write one output character.
void
consoleputc(int c)
{
  acquire(&cons);
  consputc(c);
  release(&cons);
}

// echo one input character from the UART's receive interrupt.
void
consoleintr(int c)
{
  // Lab 4 bounds UART input to a single boot hart with no line-discipline
  // edits. The byte was already dispatched by uartintr; we simply consume it.
  // (No echo is required for this lab's bounded receive contract.)
  (void)c;
}
