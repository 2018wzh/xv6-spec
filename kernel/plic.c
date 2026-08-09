#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#define PLIC_SENABLE0 (PLIC + 0x2080)
#define PLIC_SPRIORITY0 (PLIC + 0x201000)
#define PLIC_SCLAIM0 (PLIC + 0x201004)
void plicinit(void) { *(uint32 *)(PLIC + UART0_IRQ * 4) = 1; }
void plicinithart(void) { *(uint32 *)PLIC_SENABLE0 = 1 << UART0_IRQ; *(uint32 *)PLIC_SPRIORITY0 = 0; }
int plic_claim(void) { return *(uint32 *)PLIC_SCLAIM0; }
void plic_complete(int irq) { *(uint32 *)PLIC_SCLAIM0 = irq; }
