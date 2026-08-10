// proc.h - Process table, lifecycle state, and per-hart scheduler context.
//
// Lab 5 owns the fixed process table, per-process address spaces and trap
// frames, lifecycle transitions, and round-robin context-switching primitives
// on the single boot hart.

#ifndef __PROC_H__
#define __PROC_H__

// Process lifecycle states reachable in Lab 5. Every transition involving
// RUNNING, RUNNABLE, or SLEEPING holds the owning process lock; ZOMBIE enters
// scope only with a later exit/wait patch.
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Callee-saved register context exchanged by swtch.S. The field order (ra,
// sp, s0..s11) matches the symmetric save/restore slots in swtch.S so the
// context-switch oracle can compare them.
struct context {
  uint64 ra;
  uint64 sp;
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// A process slot. Its lifecycle state and mutable fields are protected by
// p->lock (process-slot-exclusivity). allocproc acquires the trap frame and
// user page table; freeproc releases both exactly once.
struct proc {
  struct spinlock lock;
  int state;              // procstate; transitions hold p->lock.
  int pid;                // unique process id.
  char name[16];
  uint64 sz;              // user memory size (0 until a later exec lab).
  uint64 kstack;          // virtual address of the slot's kernel stack.
  uint64 trapframe;       // physical trap-frame page owned by this process.
  pagetable_t pagetable;  // user page table (empty until later labs).
  struct context context; // scheduler saves/restores callee-saved state here.
  void *chan;             // sleep/wakeup channel.
};

// Per-hart state (Lab 5 runs a single boot hart but preserves per-hart
// interfaces). `proc` is the process currently running on the hart and
// `context` is the hart's scheduler context exchanged by swtch.
struct cpu {
  struct proc *proc;        // the process running on this hart (or 0).
  struct context context;   // this hart's scheduler context.
  int noff;                 // depth of interrupt-disabling (push_off).
  int intena;               // saved interrupt-enable across swtch.
};

#endif // __PROC_H__
