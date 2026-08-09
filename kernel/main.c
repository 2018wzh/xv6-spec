// main.c - supervisor-mode entry reached via start()'s mret.

#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "defs.h"

// Called from start() after it mret's into supervisor mode.
void
main(void)
{
  // The boot stage initializes memory before any page-allocation or
  // virtual-memory consumer runs (Lab 3 memory bootstrap).
  kinit();              // physical-page freelist (once the kernel image end is fixed)
  kvminit();            // construct the Sv39 kernel page table (single boot hart)
  kvminithart();        // activate the kernel page table (turn on paging)

  // Publish the Lab 2 deterministic serial banner exactly once.
  publish_boot_banner();

  // Lab 2 has no further mechanisms; park forever. Later labs extend main().
  for (;;)
    ;
}