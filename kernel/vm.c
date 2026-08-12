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

// Allocate one user page, copy the initial user program `src` (size `sz`,
// at most one page) into it, and map it at user virtual address 0 with
// user-addressed read/write/execute permissions so the first user process
// can run it. Returns 0 on success, -1 on allocation failure.
int
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("uvmfirst: too big");
  if ((mem = kalloc()) == 0)
    return -1;
  memset(mem, 0, PGSIZE);
  memmove(mem, src, sz);
  if (mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
    kfree(mem);
    return -1;
  }
  return 0;
}

// Free a process's user page table. Lab 5 user address spaces are empty
// (sz == 0), so the root page-table page is released with no leaf pages;
// a nonempty user address space is outside the current lab scope.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0 || va >= MAXVA || npages > (MAXVA - va) / PGSIZE)
    panic("uvmunmap");
  for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    pte = walk(pagetable, a, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if ((*pte & (PTE_R | PTE_W | PTE_X)) == 0)
      panic("uvmunmap: not leaf");
    if (do_free)
      kfree((void *)PTE2PA(*pte));
    *pte = 0;
  }
}

void
freewalk(pagetable_t pagetable)
{
  int i;

  for (i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz || newsz >= MAXVA)
    return oldsz;
  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE) {
    mem = kalloc();
    if (mem == 0)
      goto fail;
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0) {
      kfree(mem);
      goto fail;
    }
  }
  return newsz;
fail:
  uvmunmap(pagetable, oldsz, (a - oldsz) / PGSIZE, 1);
  return 0;
}

uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;
  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
    uvmunmap(pagetable, PGROUNDUP(newsz),
             (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE, 1);
  return newsz;
}

int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 i, pa;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE) {
    pte = walk(old, i, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if ((*pte & (PTE_R | PTE_W | PTE_X)) == 0)
      panic("uvmcopy: not leaf");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    mem = kalloc();
    if (mem == 0)
      goto fail;
    memmove(mem, (void *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto fail;
    }
  }
  return 0;
fail:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// Map the virtual address `va` to the physical `pa` for `sz` bytes with
// permissions `perm` in the given (process) page table, panicking on a
// mapping failure. Used by the syscall/trap composition to install the
// per-process TRAMPOLINE and TRAPFRAME user mappings alongside the
// kernel's TRAMPOLINE mapping.
void
uvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(pagetable, va, sz, pa, perm) != 0)
    panic("uvmmap");
}

// Translate a kernel virtual address to a physical address for a user page
// table. Returns 0 when the virtual address is not mapped as a valid leaf
// in the current user page table. Used only against user page tables and
// never dereferences a raw user-supplied address.
static uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// Copy `len` bytes from the validated user address `srcva` (in the current
// process page table) into a kernel buffer `dst`. Returns 0 on success or
// -1 when the source range is not fully mapped or overflows; no byte is
// read from an invalid user address (no raw user pointer is dereferenced).
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = (uint64)PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a NUL-terminated string from the validated user address `srcva` into
// the kernel buffer `dst`, copying through the first NUL byte within the
// finite bound `max`. Returns 0 on success or -1 when the source is not
// mapped, the string is unterminated within `max`, or the destination would
// overrun. The destination is always NUL-terminated on success.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0) {
    if (max == 0)
      return -1;
    va0 = (uint64)PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max) {
      n = max;
    }

    {
      char *p = (char *)pa0 + (srcva - va0);
      uint64 i;
      for (i = 0; i < n; i++) {
        if (p[i] == '\0') {
          *dst++ = '\0';
          got_null = 1;
          break;
        } else {
          *dst++ = p[i];
        }
      }
    }

    if (got_null)
      break;
    max -= n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy `len` bytes from the kernel buffer `src` into the validated user
// destination `dstva` (in the current process page table). Returns 0 on
// success or -1 when the destination range is not fully mapped or
// overflows; no byte is written outside the validated user pages.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = (uint64)PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
