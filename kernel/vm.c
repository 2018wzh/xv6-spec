#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
pagetable_t kernel_pagetable;
pagetable_t kvmmake(void) { pagetable_t p = (pagetable_t)kalloc(); memset(p, 0, PGSIZE); kvmmap(p, KERNBASE, KERNBASE, PHYSTOP-KERNBASE, PTE_R|PTE_W|PTE_X); kvmmap(p, UART0, UART0, PGSIZE, PTE_R|PTE_W); kvmmap(p, PLIC, PLIC, 0x4000000, PTE_R|PTE_W); return p; }
void kvminit(void) { kernel_pagetable = kvmmake(); }
void kvminithart(void) { w_satp(MAKE_SATP(kernel_pagetable)); sfence_vma(); }
void kvmmap(pagetable_t p, uint64 va, uint64 pa, uint64 size, int perm) { (void)p; (void)va; (void)pa; (void)size; (void)perm; }
