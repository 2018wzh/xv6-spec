// syscall.c - validated system call dispatch for the Lab 5 syscall surface.
//
// syscall() is invoked from usertrap after a user ecall is identified and
// sepc advanced by one instruction. It bounds-checks the syscall number in
// trap-frame a7 against the dispatch table before any table access, calls
// the named handler exactly once, and publishes the single result back to
// trap-frame a0. A number outside the table (or a handler-less slot) returns
// -1 with a bounded diagnostic and never indexes outside the table.
//
// All user-supplied arguments cross only through the validated copy helpers
// in kernel/vm.c (copyin/copyinstr/copyout); C code never dereferences a raw
// user virtual address.
//
// sysproc.c provides the concrete Lab 5 process-control and console-call
// handlers.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at user address addr into *ip. Returns 0 on success, -1
// when the 8-byte range is not fully mapped in the current process page
// table (validated, no raw user dereference).
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();

  if (addr >= p->sz || addr + sizeof(uint64) > p->sz)
    return -1;
  if (copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the uint64 at trap-frame slot n (n < 32) into *ip. Arguments
// originate only in the current process trap frame. The argument registers
// a0..a7 occupy slots 12..19 of the fixed trap-frame ABI.
int
fetchargint(int n, uint64 *ip)
{
  struct usertrapframe *tf = (struct usertrapframe *)myproc()->trapframe;

  if (n < 0 || n >= 6)
    return -1;
  *ip = *(uint64 *)((char *)tf + (n + 12) * 8);
  return 0;
}

// Fetch the int at trap-frame arg slot n. Returns 0 on success, -1 when the
// slot index is out of range.
int
argint(int n, int *ip)
{
  uint64 x;
  if (fetchargint(n, &x) < 0)
    return -1;
  *ip = (int)x;
  return 0;
}

// Fetch the uint64 address at trap-frame arg slot n.
int
argaddr(int n, uint64 *ip)
{
  return fetchargint(n, ip);
}

// Fetch the NUL-terminated string at user address addr into *buf. If a
// kernel page is not present or the string is not terminated within max
// bytes, return -1 without overrunning buf. The maximum accepted string
// length is bounded by max.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();

  if (addr >= p->sz)
    return -1;
  if (copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

// Fetch the NUL-terminated string argument from trap-frame arg slot n and
// store it in *pp, which must point to a buf of size max. Returns the string
// length on success or -1 on any validation failure.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if (argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

// Pointers to the Lab 5 syscall handlers in kernel/sysproc.c. Any slot left
// as 0 is rejected by syscall() before being called.
extern uint64 sys_exit(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_uptime(void);
extern uint64 sys_fork(void);
extern uint64 sys_wait(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);

// Pointers to the Lab 6 file syscall handlers in kernel/sysfile.c.
extern uint64 sys_open(void);
extern uint64 sys_read(void);
extern uint64 sys_write(void);
extern uint64 sys_close(void);
extern uint64 sys_fstat(void);
extern uint64 sys_dup(void);
extern uint64 sys_mknod(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_chdir(void);
extern uint64 sys_link(void);
extern uint64 sys_unlink(void);
extern uint64 sys_pipe(void);

// The dispatch table, indexed by the validated syscall number. The table has
// explicit SYS_MAX+1 entries so that every number up to SYS_MAX maps in-bounds.
static uint64 (*syscalls[SYS_MAX + 1])(void) = {
  [SYS_fork]    sys_fork,
  [SYS_exit]    sys_exit,
  [SYS_wait]    sys_wait,
  [SYS_pipe]    sys_pipe,
  [SYS_read]    sys_read,
  [SYS_kill]    sys_kill,
  [SYS_exec]    sys_exec,
  [SYS_fstat]   sys_fstat,
  [SYS_chdir]   sys_chdir,
  [SYS_dup]     sys_dup,
  [SYS_getpid]  sys_getpid,
  [SYS_sbrk]    sys_sbrk,
  [SYS_sleep]   0,
  [SYS_uptime]  sys_uptime,
  [SYS_open]    sys_open,
  [SYS_write]   sys_write,
  [SYS_mknod]   sys_mknod,
  [SYS_unlink]  sys_unlink,
  [SYS_link]    sys_link,
  [SYS_mkdir]   sys_mkdir,
  [SYS_close]   sys_close,
};

// Dispatch the current user ecall. num must be in [0, SYS_MAX]; the floor is
// enforced (num <= 0 returns -1, satisfying the "positive and non-null"
// property), and the ceiling is checked before any table access. Publishes
// the handler result into trap-frame a0 exactly once.
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe ? ((struct usertrapframe *)p->trapframe)->a7 : 999;

  if (num > 0 && num <= SYS_MAX && syscalls[num] != 0) {
    // Valid, bounded, handler-present: dispatch exactly once and publish the
    // result to trap-frame a0.
    ((struct usertrapframe *)p->trapframe)->a0 = syscalls[num]();
  } else {
    // Unknown or handler-less call: return -1 and emit a bounded diagnostic;
    // num is validated so we never index outside the table.
    if (p->trapframe)
      ((struct usertrapframe *)p->trapframe)->a0 = -1;
    printf("syscall %d: not implemented\n", num);
  }
}
