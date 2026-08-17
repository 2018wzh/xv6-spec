// sysproc.c - the concrete Lab 5 syscall handlers: minimal process-control
// and console-call surface, all arguments validated through the copy helpers.
//
// Only the operations declared by the kernel/syscall module are implemented.
// Each handler fetches its arguments only from the current process trap frame
// (integer/address slots) or from validated user memory (copyin/copyinstr);
// no ra know user virtual address is directly dereferenced.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Return the current process's unique pid.
uint64
sys_getpid(void)
{
  return myproc()->pid;
}

// Grow or shrink the process's user memory size. Lab 5 user address spaces
// hold only the embedded initcode (no dynamic allocation), so sbrk returns
// the current size without adjusting any mapping.
uint64
sys_sbrk(void)
{
  int n;
  struct proc *p = myproc();

  if (argint(0, &n) < 0)
    return -1;
  return p->sz;
}

// Return the number of clock ticks since boot, read from the machine timer.
uint64
sys_uptime(void)
{
  uint64 xticks;
  xticks = *(volatile uint64 *)(CLINT_MTIME);  // machine-mode timer, host-visible
  return xticks;
}

// Terminate the current process. Lab 5 has no futher exit/wait scope, so
// this is a minimal stub that parks the process; it is not reached by the
// first user process.
uint64
sys_exit(void)
{
  int status;
  if (argint(0, &status) < 0)
    return -1;
  exit(status);
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;
  return wait(addr);
}

uint64
sys_kill(void)
{
  int pid;
  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  uint64 uargv, uarg;
  int i, result = -1;
  if (argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0)
    return -1;
  memset(argv, 0, sizeof(argv));
  for (i = 0; i < MAXARG; i++) {
    if (fetchaddr(uargv + sizeof(uint64) * i, &uarg) < 0)
      goto bad;
    if (uarg == 0) {
      argv[i] = 0;
      result = exec(path, argv);
      goto done;
    }
    if ((argv[i] = kalloc()) == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }
bad:
  result = -1;
done:
  for (i = 0; i < MAXARG; i++)
    if (argv[i]) kfree(argv[i]);
  return result;
}
