#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "platform.h"

static uint64
plic_context_base(void)
{
  return platform_get()->plic_base + 0x201000 +
         platform_hartid(cpuid()) * 0x2000;
}

static void
plic_enable(uint32 irq)
{
  uint64 base = platform_get()->plic_base + 0x2080 +
                platform_hartid(cpuid()) * 0x100;
  volatile uint32 *word = (volatile uint32 *)(base + (irq / 32) * 4);
  *word |= 1U << (irq % 32);
}

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  const struct platform_info *p = platform_get();
  *(volatile uint32 *)(p->plic_base + p->uart_irq * 4) = 1;
  *(volatile uint32 *)(p->plic_base + p->block_irq * 4) = 1;
}

void
plicinithart(void)
{
  const struct platform_info *p = platform_get();
  plic_enable(p->uart_irq);
  // SD I/O is polled with a global spinlock. Only the boot hart enables the
  // block IRQ so a pending SD/PLIC event cannot be claimed by a secondary
  // hart while the boot hart is mid-command.
  if (cpuid() == 0)
    plic_enable(p->block_irq);

  // set this hart's S-mode priority threshold to 0.
  *(volatile uint32 *)plic_context_base() = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  return *(volatile uint32 *)(plic_context_base() + 4);
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  *(volatile uint32 *)(plic_context_base() + 4) = irq;
}
