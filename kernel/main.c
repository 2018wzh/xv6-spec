// main.c - supervisor-mode entry reached via start()'s mret.

#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "buf.h"
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

  // Lab 6 virtio storage bootstrap: negotiate the block device and prepare
  // the descriptor/available/used rings before any file-system layer that
  // transfers logical blocks runs (fsinit is a later slice; "before fsinit"
  // means before any storage consumer).
  virtio_disk_init();

  // Lab 6 file-system mount (kernel/inode). The buffer cache and redo log are
  // initialized here, and fsinit mounts the deterministic mkfs-generated root
  // image (validating geometry and completing log recovery) before any user
  // file operation is admitted (filesystem-admission-order). An unformatted
  // root disk leaves the file system unmounted and still reaches the
  // scheduler, so the storage-layered kernel boots on a bare lab disk.
  binit();
  fsinit(1);

  // Lab 6 file ABI: initialize the global file table before any process
  // descriptor can hold a file reference (kernel/file).
  fileinit();

  // Lab 5 process substrate: initialize the process table (and each slot's
  // index-keyed kernel stack mapping) before the scheduler can activate.
  procinit();

  // Lab 5 syscall composition: create the first user process (loads initcode
  // and maps TRAMPOLINE/TRAPFRAME) so the scheduler can dispatch it across
  // the trap-frame privilege boundary.
  userinit();

  // Only now, with stvec, device routing, the process table, and the first
  // user process established, enable supervisor interrupts.
  intr_on();

  // Lab 5 round-robin scheduler: the single boot hart scans the process
  // table for RUNNABLE processes forever, dispatching the first user process
  // which enters user mode, performs one validated syscall, and returns.
  scheduler();
  for (;;)
    ;
}