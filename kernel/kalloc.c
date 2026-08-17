// kalloc.c - Physical page allocator: a spinlock-protected singly linked
// freelist of 4096-byte aligned physical pages, bounded by the linker symbol
// `end` and PHYSTOP.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Poison byte written into a free page. It may exist only while a page is on
// the freelist; kalloc clears it (with a zero fill) before ownership transfer.
#define KALLOC_POISON 0x5a

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after the linked kernel image

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// Initialize the allocator: expose every complete free page from the first
// 4096-byte-aligned byte after the kernel image through PHYSTOP exactly once.
void
kinit(void)
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;

  p = (char *)PGROUNDUP((uint64)pa_start);
  if ((uint64)pa_start >= (uint64)pa_end)
    panic("freerange: empty");
  if ((uint64)pa_end > PHYSTOP)
    panic("freerange: end out of bounds");
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory at pa, which must be page aligned, at or
// above the kernel image, and below PHYSTOP. The page is poisoned and pushed
// onto the freelist exactly once.
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with poison while the page is free; kalloc clears it on hand-off.
  memset(pa, KALLOC_POISON, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte-aligned page of physical memory, or return null when
// the freelist is exhausted. Every successful allocation is zero-filled so no
// freelist poison is observable after ownership transfer.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 0, PGSIZE); // zero-filled: clears any poison
  return (void *)r;
}
