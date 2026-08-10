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

  // Publish the Lab 2 deterministic serial banner exactly once (boot owns
  // this direct polling publication; the trap stage takes over afterward).
  publish_boot_banner();

  // Lab 4 trap stage: install the trap vector and configure device routing
  // (PLIC then UART) before setting the supervisor interrupt-enable bits.
  trapinit();            // stvec = kernelvec (before any interrupt can arrive)
  plicinit();            // configure UART IRQ priority
  plicinithart();        // enable UART IRQ for this hart's S-mode context
  uartinit();            // re-establish UART config and enable FIFO interrupts
  consoleinit();         // console owns ordinary output after this handoff

  // Lab 5 process substrate: initialize the process table (and each slot's
  // index-keyed kernel stack mapping) before the scheduler can activate.
  procinit();

  // Only now, with stvec, device routing, and the process table established,
  // enable supervisor interrupts.
  intr_on();

  // Lab 5 round-robin scheduler: the single boot hart scans the process
  // table for RUNNABLE processes forever. No user program exists in this
  // process-only commit, so the scheduler parks while there is nothing to
  // run, preserving a buildable intermediate kernel.
  scheduler();
  for (;;)
    ;
}