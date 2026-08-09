#include "types.h"
#include "riscv.h"
#include "defs.h"
extern void kernelvec(void);
void trapinit(void) {}
void trapinithart(void) { w_stvec((uint64)kernelvec); }
int devintr(void) {
  uint64 cause = r_scause();
  if(cause == 0x8000000000000005L) { w_stimecmp(r_time() + 1000000); return 2; }
  if(cause == 0x8000000000000009L) { int irq = plic_claim(); if(irq == 10) uartintr(); if(irq) plic_complete(irq); return 1; }
  return 0;
}
void kerneltrap(void) { if(devintr() == 0) for(;;) {} }
