// vm.h - Kernel virtual memory: Sv39 kernel page table construction.

#ifndef __VM_H__
#define __VM_H__

// the kernel's page table
extern pagetable_t kernel_pagetable;

// Map the virtual address `va` to physical `pa` for `sz` bytes with perm in
// the given page table, panicking if the mapping cannot be constructed. Used
// by procinit to install each process slot's index-keyed kernel stack.
void kvmmap(pagetable_t, uint64, uint64, uint64, int);

// Map `va` to `pa` for `sz` bytes in a user (process) page table, panicking
// on failure. Used by the syscall/trap composition to install the per-process
// TRAMPOLINE and TRAPFRAME user mappings.
void uvmmap(pagetable_t, uint64, uint64, uint64, int);

// Allocate one user page, copy `src` (size `sz`) into it, and map it at user
// virtual address 0 with user read/write/execute permissions.
int uvmfirst(pagetable_t, uchar *, uint);
uint64 uvmalloc(pagetable_t, uint64, uint64, int);
uint64 uvmdealloc(pagetable_t, uint64, uint64);
int uvmcopy(pagetable_t, pagetable_t, uint64);
void uvmunmap(pagetable_t, uint64, uint64, int);
void freewalk(pagetable_t);

#endif // __VM_H__
#include "types.h"
