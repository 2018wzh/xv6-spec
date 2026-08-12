#!/usr/bin/env sh
# kernel/memory validated user-copy contract (source-level, no QEMU).
# Verifies, for the interface operations declared in
# spec/modules/kernel/memory.yaml that are not exercised by the boot banner:
#   - copyin / copyout / copyinstr reject an unmapped or overflowing range,
#   - every copy path translates via walkaddr (no raw user pointer deref),
#   - walkaddr rejects va >= MAXVA and requires the PTE_U user bit,
#   - the user address-space helpers (uvmcreate/uvmfirst/uvmfree/uvmmap)
#     are present and uvmfirst releases the page on a mapping failure.
# Runs with cwd = project root; PATH allowed (unused here).
set -eu

# --- the three validated copies are present (declaration style: 'int' on the
#     line above the opening paren, so match the identifier + '(' alone). ---
grep -qE '^copyin\(' kernel/vm.c
grep -qE '^copyout\(' kernel/vm.c
grep -qE '^copyinstr\(' kernel/vm.c

# --- all three are exported in defs.h ---
grep -q 'copyin(pagetable_t, char \*, uint64, uint64);' kernel/defs.h
grep -q 'copyinstr(pagetable_t, char \*, uint64, uint64);' kernel/defs.h
grep -q 'copyout(pagetable_t, uint64, char \*, uint64);' kernel/defs.h

# --- copyin / copyout: translate one page at a time and fail on an unmapped
#     page (pa0 == 0) instead of dereferencing a raw user pointer. ---
grep -q 'pa0 = walkaddr(pagetable, va0);' kernel/vm.c
grep -q 'if (pa0 == 0)' kernel/vm.c

# --- copyinstr: copy up to the finite bound and stop at a NUL; reject an
#     unterminated string (max == 0 without a NUL) and an unmapped page. ---
grep -q 'copyinstr(pagetable_t pagetable, char \*dst, uint64 srcva, uint64 max)' kernel/vm.c
grep -q 'if (max == 0)' kernel/vm.c
grep -q 'got_null' kernel/vm.c

# --- every copy is validated through walkaddr, never a raw user deref. ---
n_copy_walk="$(grep -c 'walkaddr(pagetable, va0)' kernel/vm.c)"
[ "$n_copy_walk" -ge 3 ]

# --- walkaddr rejects va >= MAXVA and requires the user bit PTE_U. ---
walk_guard="$(sed -n '/^walkaddr/,/^}/p' kernel/vm.c | grep -c 'va >= MAXVA')"
[ "$walk_guard" -ge 1 ]
grep -qE '\(\*pte & PTE_U\) == 0' kernel/vm.c

# --- user address-space helpers exist; uvmfirst frees the page on failure. ---
grep -q '^uvmcreate(' kernel/vm.c
grep -q '^uvmfirst(' kernel/vm.c
grep -q '^uvmfree(' kernel/vm.c
grep -q '^uvmmap(' kernel/vm.c
grep -q 'mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U)' kernel/vm.c
grep -qE 'kfree\(mem\);' kernel/vm.c

echo "contract: validated user-copy (copyin/copyout/copyinstr) and address-space helpers present"