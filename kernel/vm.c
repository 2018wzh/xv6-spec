// vm.c - Sv39 kernel page table. A conventional three-level walk builds the
// kernel's page table; mappings never set the user-accessible bit and every
// installed leaf mapping uses the least privilege declared by its region.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

pagetable_t kernel_pagetable;

extern char etext[];      // kernel.ld sets this to end of kernel code
extern char trampoline[]; // kernel.ld sets this to the trampoline page

pagetable_t kvmmake(void);
void kvmmap(pagetable_t, uint64, uint64, uint64, int);
pde_t *walk(pagetable_t, uint64, int);
int mappages(pagetable_t, uint64, uint64, uint64, int);

// Return the address of the PTE in page table `pagetable` that corresponds
// to virtual address `va`. Allocates intermediate page-table pages if `alloc`.
pde_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  int level;

  if (va >= MAXVA)
    panic("walk");

  for (level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Create PTEs for virtual addresses starting at va that refer to physical
// addresses starting at pa. va and size must not cross MAXVA; va must not be
// remapped.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pde_t *pte;

  if (size == 0)
    panic("mappages: size");

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Map the virtual address `va` to the physical address `pa` for `sz` bytes
// with permissions `perm`, or panic if the mapping cannot be constructed.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Construct the kernel page table.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers (writable, no execute, no user).
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface.
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC.
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // kernel text: executable but not writable.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // kernel data and the physical RAM we will make use of: writable, no
  // execute, no user bit.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // trampoline at the highest virtual address of the kernel address space.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  return kpgtbl;
}

// Build the kernel page table. Runs on the single boot hart before any
// concurrent execution; after activation the mappings are treated as
// immutable for this lab.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch to the kernel page table and flush the TLB.
void
kvminithart(void)
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Create an empty user page table: a single zeroed Sv39 root page with no
// mappings. Each Lab 5 process owns one; user mappings land in a later lab.
pagetable_t
uvmcreate(void)
{
  pagetable_t pagetable;

  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Free a process's user page table. Lab 5 user address spaces are empty
// (sz == 0), so the root page-table page is released with no leaf pages;
// a nonempty user address space is outside the current lab scope.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz != 0)
    panic("uvmfree: nonempty user address space outside Lab 5 scope");
  kfree((void *)pagetable);
}