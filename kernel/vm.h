// vm.h - Kernel virtual memory: Sv39 kernel page table construction.

#ifndef __VM_H__
#define __VM_H__

// the kernel's page table
extern pagetable_t kernel_pagetable;

// Map the virtual address `va` to physical `pa` for `sz` bytes with perm in
// the given page table, panicking if the mapping cannot be constructed. Used
// by procinit to install each process slot's index-keyed kernel stack.
void kvmmap(pagetable_t, uint64, uint64, uint64, int);

#endif // __VM_H__
