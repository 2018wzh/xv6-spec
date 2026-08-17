#!/usr/bin/env sh
# kernel/memory contract check (source-level, no QEMU). Verifies the module
# invariants declared in spec/modules/kernel/memory.yaml:
#   - freelist-integrity / freelist-start-boundary,
#   - poison-is-not-observable,
#   - sv39-three-level-walk and MAXVA rejection,
#   - mapping-isolation (no PTE_U in kernel mappings),
#   - kernel mapping permissions (text R|X, data R|W, devices R|W).
# Runs with cwd = project root; PATH allowed (unused here).
set -eu

# --- allocator: freelist + lock ---
grep -q 'struct run \*freelist' kernel/kalloc.c
grep -q 'struct spinlock lock' kernel/kalloc.c
grep -q 'initlock(&kmem.lock' kernel/kalloc.c

# --- allocator: freelist-start-boundary (round_up(end, 4096), PHYSTOP bound) ---
grep -q 'PGROUNDUP((uint64)pa_start)' kernel/kalloc.c
grep -q 'PHYSTOP' kernel/kalloc.c
grep -q 'end\b' kernel/kalloc.c

# --- allocator: poison-is-not-observable (poison only while free; kalloc clears) ---
grep -q 'KALLOC_POISON' kernel/kalloc.c
grep -q 'memset(pa, KALLOC_POISON, PGSIZE)' kernel/kalloc.c
grep -q 'memset((char \*)r, 0, PGSIZE)' kernel/kalloc.c

# --- allocator: invalid inputs fail before state mutation ---
grep -q 'panic("kfree")' kernel/kalloc.c
grep -q 'panic("freerange: end out of bounds")' kernel/kalloc.c

# --- allocator: freelist mutations hold the allocator lock ---
grep -q 'acquire(&kmem.lock)' kernel/kalloc.c
grep -q 'release(&kmem.lock)' kernel/kalloc.c

# --- Sv39 three-level walk + MAXVA rejection in vm.c ---
grep -q 'define PXSHIFT' kernel/memlayout.h
grep -q 'define MAXVA' kernel/memlayout.h
grep -q 'PX(level, va)' kernel/vm.c
grep -q 'for (level = 2; level > 0; level--)' kernel/vm.c
grep -q 'if (va >= MAXVA)' kernel/vm.c

# --- mapping-isolation: kernel mappings never set PTE_U ---
grep -q 'PTE_U' kernel/memlayout.h
# The fragment below will NOT match any kvmmap call (no PTE_U passthrough).
if grep -E 'kvmmap\(' kernel/vm.c | grep -q 'PTE_U'; then
  echo "contract: unexpected PTE_U in a kernel mapping" >&2
  exit 1
fi

# --- kernel mapping permissions (least privilege per region) ---
grep -qE 'kvmmap\(kpgtbl, KERNBASE, KERNBASE, \(uint64\)etext - KERNBASE, PTE_R \\?| PTE_X' kernel/vm.c
grep -qE 'PHYSTOP - \(uint64\)etext' kernel/vm.c
grep -q 'kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W)' kernel/vm.c
grep -q 'kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X)' kernel/vm.c

# --- mapping-publication: kernel page table built before concurrent use ---
grep -q 'kvmmake(void)' kernel/vm.c
grep -q 'kernel_pagetable = kvmmake()' kernel/vm.c

echo "contract: freelist, poison, Sv39 walk, and kernel mapping invariants present"
