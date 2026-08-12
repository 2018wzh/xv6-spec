// proc.c - the Lab 5 process substrate: a fixed process table, per-process
// address spaces and trap frames, lifecycle transitions, sleep/wakeup, and
// the round-robin scheduler on the single boot hart.
//
// procinit assigns one deterministic index-keyed kernel stack mapping to each
// process slot and initializes every slot's lock and UNUSED state before the
// scheduler can activate. allocproc acquires a trap frame and an empty user
// page table for a USED slot and initializes its context; freeproc releases
// both exactly once and returns the slot to UNUSED.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "buf.h"
#include "proc.h"
#include "vm.h"
#include "fs.h"
#include "file.h"
#include "defs.h"

struct cpu cpus[NCPU];       // per-hart state (single boot hart is cpus[0]).
struct proc proc[NPROC];     // the fixed process table.

static int nextpid = 1;      // next unique process id.
static struct spinlock wait_lock;

static void allocpid(struct proc *p);
static void freeproc(struct proc *p);
static void forkret(void);

// Return the process running on the current hart (single boot hart).
struct proc *
myproc(void)
{
  return cpus[0].proc;
}

// Assign the next unique process id while holding p->lock.
static void
allocpid(struct proc *p)
{
  p->pid = nextpid;
  nextpid = p->pid + 1;
}

// Initialize the process table before scheduler activation. Assigns one
// deterministic index-keyed kernel stack mapping per slot for the kernel
// lifetime; allocproc and freeproc reuse it. Kernel-stack allocation or
// mapping failure panics before scheduling.
void
procinit(void)
{
  struct proc *p;

  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++) {
    uint64 pa;
    initlock(&p->lock, "proc");
    p->kstack = KSTACK((int)(p - proc));

    // Map the slot's kernel stack into the kernel page table at its
    // index-keyed virtual address. The mapping persists for the kernel
    // lifetime and is reused by allocproc/freeproc.
    pa = (uint64)kalloc();
    if (pa == 0)
      panic("procinit: kernel-stack allocation");
    kvmmap(kernel_pagetable, p->kstack, pa, PGSIZE, PTE_R | PTE_W);

    p->state = UNUSED;
  }
  cpus[0].noff = 0;
  cpus[0].intena = 0;
  cpus[0].proc = 0;
}

// Look up a process table entry. If found, initialize the state required to
// run the process in user mode (with no user mappings until a later exec
// lab), then return it with p->lock held and state USED. Returns 0 without
// leaking resources when the table is exhausted.
struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED)
      goto found;
    release(&p->lock);
  }
  return 0;

found:
  allocpid(p);
  p->state = USED;
  p->sz = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->parent = 0;
  safestrcpy(p->name, "proc", sizeof(p->name));
  {
    int i;
    for (i = 0; i < NOFILE; i++)
      p->ofile[i] = 0;   // Lab 6: a reused slot starts with no file refs.
    p->cwd = 0;          // kernel/file set on the first chdir/open lifecycle.
  }

  // Acquire a trap-frame page. In Lab 5 the trap frame is a physical page
  // owned by this process (its user page-table TRAPFRAME mapping lands with
  // the syscall/trap composition).
  if ((p->trapframe = (uint64)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table: there are no user mappings until a later
  // exec lab, so allocate only the root page-table page.
  if ((p->pagetable = uvmcreate()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up the process context so the scheduler can switch to it: the first
  // dispatch enters forkret on the slot's kernel stack.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;  // returned with p->lock held; caller must release.
}

// Release a process's resources and return its slot to UNUSED. Prepares the
// slot for reuse without releasing its index-keyed kernel stack mapping.
// Must be called with p->lock held for a non-RUNNING slot. Each resource is
// released exactly once and cleared so a partial rollback is idempotent.
static void
freeproc(struct proc *p)
{
  if (p->trapframe) {
    kfree((void *)p->trapframe);
    p->trapframe = 0;
  }
  if (p->pagetable) {
    uvmfree(p->pagetable, p->sz);
    p->pagetable = 0;
  }
  p->sz = 0;
  p->pid = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->parent = 0;
  memset(&p->context, 0, sizeof(p->context));
  p->state = UNUSED;
}

// Entered on the first dispatch into a newly allocated process. The
// scheduler holds the process lock across the first swtch, so forkret
// releases it. The process then returns to user mode for the first time via
// usertrapret (the syscall/trap composition installs the uservec/userret
// trampoline and its trap frame).
static void
forkret(void)
{
  release(&myproc()->lock);
  usertrapret();  // enter user mode for the first time.
}

// Round-robin scheduler: scan the process table for RUNNABLE processes and
// dispatch each while holding its lock. Interrupts are enabled between scans
// and disabled across each swtch (exactly one process lock is held).
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = &cpus[0];

  c->proc = 0;
  for (;;) {
    intr_on();  // interrupts enabled between scans.
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        // Returned after the process called sched to yield back to us.
        c->proc = 0;
      }
      release(&p->lock);
    }
    if (intr_get())
      intr_off();
  }
}

// Hand over the current process back to the scheduler. Must be called with
// the caller's process lock held, interrupts disabled, and the process in
// RUNNING; returns after the process is rescheduled and the lock reacquired.
void
sched(void)
{
  struct proc *p = myproc();

  if (holding(&p->lock) == 0)
    panic("sched: process lock not held");
  if (intr_get())
    panic("sched: interruptible");
  if (p->state == RUNNING)
    panic("sched: still RUNNING");

  swtch(&p->context, &cpus[0].context);
  // Reacquired p->lock is restored by the caller on return from yield/sleep.
}

// Clone the current process. The child is not published RUNNABLE until its
// address space, trap frame, descriptors, cwd, and parent link are complete.
int
fork(void)
{
  int i, pid;
  struct proc *p = myproc();
  struct proc *np = allocproc();

  if (np == 0)
    return -1;
  uvmmap(np->pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  uvmmap(np->pagetable, TRAPFRAME, np->trapframe, PGSIZE, PTE_R | PTE_W | PTE_U);
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    uvmunmap(np->pagetable, TRAPFRAME, 1, 0);
    uvmunmap(np->pagetable, TRAMPOLINE, 1, 0);
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  *(struct usertrapframe *)np->trapframe = *(struct usertrapframe *)p->trapframe;
  ((struct usertrapframe *)np->trapframe)->a0 = 0;
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  if (p->cwd)
    np->cwd = idup(p->cwd);
  safestrcpy(np->name, p->name, sizeof(np->name));
  pid = np->pid;
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);
  release(&wait_lock);
  return pid;
}

static void
reparent(struct proc *p)
{
  struct proc *pp;
  for (pp = proc; pp < &proc[NPROC]; pp++)
    if (pp->parent == p) {
      pp->parent = &proc[0];
      wakeup(&proc[0]);
    }
}

void
exit(int status)
{
  int fd;
  struct proc *p = myproc();
  if (p == &proc[0])
    panic("init exiting");
  for (fd = 0; fd < NOFILE; fd++)
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      p->ofile[fd] = 0;
      fileclose(f);
    }
  begin_op();
  if (p->cwd) {
    iput(p->cwd);
    p->cwd = 0;
  }
  end_op();
  acquire(&wait_lock);
  reparent(p);
  wakeup(p->parent);
  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;
  release(&wait_lock);
  sched();
  panic("zombie exit");
  for (;;)
    ;
}

int
wait(uint64 addr)
{
  struct proc *pp;
  struct proc *p = myproc();
  int havekids, pid;

  acquire(&wait_lock);
  for (;;) {
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent != p)
        continue;
      acquire(&pp->lock);
      havekids = 1;
      if (pp->state == ZOMBIE) {
        pid = pp->pid;
        if (addr && copyout(p->pagetable, addr, (char *)&pp->xstate,
                            sizeof(pp->xstate)) < 0) {
          release(&pp->lock);
          release(&wait_lock);
          return -1;
        }
        if (pp->pagetable) {
          uvmunmap(pp->pagetable, TRAPFRAME, 1, 0);
          uvmunmap(pp->pagetable, TRAMPOLINE, 1, 0);
        }
        freeproc(pp);
        release(&pp->lock);
        release(&wait_lock);
        return pid;
      }
      release(&pp->lock);
    }
    if (!havekids || p->killed) {
      release(&wait_lock);
      return -1;
    }
    sleep(p, &wait_lock);
  }
}

int
kill(int pid)
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED) {
      p->killed = 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Give up the CPU for one scheduling round while holding p->lock.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// Atomically release the caller's lock lk and switch the current process to
// SLEEPING on channel chan; reacquire lk after being woken and rescheduled.
// The lock handoff (acquire p->lock, then release lk) prevents a lost wakeup.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  release(lk);
  p->chan = chan;
  p->state = SLEEPING;
  sched();
  // Rescheduled; clean up and reacquire the caller's lock.
  p->chan = 0;
  release(&p->lock);
  acquire(lk);
}

// Wake all processes sleeping on channel chan, marking matching SLEEPING
// processes RUNNABLE while holding their locks.
void
wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
        p->state = RUNNABLE;
      release(&p->lock);
    }
  }
}

// The first user program, as little-endian RV64 machine code (assembled from
// the source below). It is loaded at user virtual address 0 into the first
// process and makes one validated syscall: write(1, msg, 12) so the Lab 5
// console-call surface is exercised and the ecall return path is observable
// on serial. It then loops forever so the booted kernel stays live in QEMU.
//
//   li a7, 16      ; SYS_write
//   li a0, 1       ; fd = console
//   li a1, 14      ; msg address (offset 14 within code at VA 0)
//   li a2, 12      ; len = "SYSCALL_OK\n"
//   ecall
// 1: j 1b
// msg: .asciz "SYSCALL_OK\n"
static uchar initcode[] = {
  0xc1, 0x48, 0x05, 0x45, 0xb9, 0x45, 0x31, 0x46, 0x73, 0x00, 0x00, 0x00,
  0x01, 0xa0, 0x53, 0x59, 0x53, 0x43, 0x41, 0x4c, 0x4c, 0x5f, 0x4f, 0x4b,
  0x0a, 0x00
};

// Create and run the first user process. It acquires one USED slot, loads
// initcode into its user page table at VA 0, maps TRAMPOLINE and TRAPFRAME
// into that page table, initializes its trap frame for a first user-mode
// entry, and publishes the slot as RUNNABLE for the scheduler.
void
userinit(void)
{
  struct proc *p;
  uint64 trampoline_pa = (uint64)trampoline;

  p = allocproc();  // returns with p->lock held.
  if (p == 0)
    panic("userinit: allocproc");

  // Map the shared trampoline page (read-execute, no user bit) and the
  // process's trap-frame page into the user page table at the canonical
  // TRAMPOLINE/TRAPFRAME virtual addresses.
  uvmmap(p->pagetable, TRAMPOLINE, trampoline_pa, PGSIZE, PTE_R | PTE_X);
  uvmmap(p->pagetable, TRAPFRAME, p->trapframe, PGSIZE, PTE_R | PTE_W | PTE_U);

  // Load the init user program into one page at user VA 0.
  if (uvmfirst(p->pagetable, initcode, sizeof(initcode)) < 0)
    panic("userinit: uvmfirst");
  p->sz = PGSIZE;
  safestrcpy(p->name, "initcode", sizeof(p->name));

  // Set up the trap frame so the first usertrapret enters user mode at the
  // start of initcode.
  {
    struct usertrapframe *tf = (struct usertrapframe *)p->trapframe;
    memset(tf, 0, sizeof(*tf));
    tf->kernel_sp = p->kstack + PGSIZE;  // set fresh by usertrapret too
    tf->kernel_trap = (uint64)usertrap;
    tf->epc = 0;                         // first user instruction
  }

  // Publish as runnable. The scheduler will dispatch it first.
  p->state = RUNNABLE;
  release(&p->lock);
}
