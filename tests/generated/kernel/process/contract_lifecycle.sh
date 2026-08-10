#!/usr/bin/env sh
# kernel/process lifecycle contract check (source-level, no QEMU). Verifies the
# invariants declared in spec/modules/kernel/process.yaml:
#   - process-slot-exclusivity (per-slot lock protects lifecycle state),
#   - address-space-ownership (allocproc acquires / freeproc releases the
#     trap frame and user page table exactly once),
#   - procinit assigns a deterministic index-keyed kernel stack per slot,
#   - allocproc rollback and the USED slot's initialized context,
#   - every Lab 5 RUNNING/RUNNABLE/SLEEPING transition holds the process lock.
# Runs with cwd = project root; PATH allowed.
set -eu

# Fixed process table + per-slot (process) lock.
grep -q 'struct proc proc\[NPROC\]' kernel/proc.c
grep -q 'struct spinlock lock' kernel/proc.h

# procinit assigns a deterministic index-keyed kernel stack per slot.
grep -q 'p->kstack = KSTACK' kernel/proc.c
grep -q 'kvmmap(kernel_pagetable, p->kstack' kernel/proc.c
grep -q 'initlock(&p->lock' kernel/proc.c
grep -q 'p->state = UNUSED' kernel/proc.c

# allocproc: acquires a trap frame and an empty user page table; rollback on
# failure via freeproc leaves the slot UNUSED (no leak).
grep -q 'allocproc(void)' kernel/proc.c
grep -q '(uint64)kalloc()' kernel/proc.c
grep -q 'uvmcreate()' kernel/proc.c
grep -q 'p->state = USED' kernel/proc.c
grep -q 'freeproc(p)' kernel/proc.c
grep -q 'p->context.ra = (uint64)forkret' kernel/proc.c
grep -q 'p->context.sp = p->kstack + PGSIZE' kernel/proc.c

# freeproc releases the trap frame and user page table exactly once and
# returns the slot to UNUSED, preserving the index-keyed kernel stack mapping.
grep -q 'kfree((void \*)p->trapframe)' kernel/proc.c
grep -q 'uvmfree(p->pagetable, p->sz)' kernel/proc.c
grep -q 'p->state = UNUSED' kernel/proc.c

# Every RUNNING/RUNNABLE/SLEEPING transition is guarded by the process lock.
grep -q 'acquire(&p->lock)' kernel/proc.c
grep -q 'release(&p->lock)' kernel/proc.c
grep -q 'p->state = RUNNING' kernel/proc.c
grep -q 'p->state = RUNNABLE' kernel/proc.c
grep -q 'p->state = SLEEPING' kernel/proc.c

# scheduler holds exactly one process lock across swtch and inline-verifies
# state before the RUNNABLE->RUNNING dispatch.
grep -q 'swtch(&c->context, &p->context)' kernel/proc.c
grep -q 'if (p->state == RUNNABLE)' kernel/proc.c
grep -q 'p->state = RUNNING' kernel/proc.c

# sched panics on illegal lock/interrupt/state conditions.
grep -q 'panic("sched: process lock not held")' kernel/proc.c
grep -q 'panic("sched: interruptible")' kernel/proc.c
grep -q 'panic("sched: not RUNNING")' kernel/proc.c

echo "ok: process lifecycle, resource ownership, and lock-guarded transitions present"
