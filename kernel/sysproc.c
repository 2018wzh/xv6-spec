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
  for (;;)
    ;
}

// Write to the console. `write(1, addr, n)` copies n bytes from the
// validated user address `addr` and emits them on the console. This is the
// console-call surface of the Lab 5 syscall module and is what the first
// user process uses to publish its single bounded ecall result.
uint64
sys_write(void)
{
  int fd, n;
  uint64 addr;
  struct proc *p = myproc();
  char buf[64];

  if (argint(0, &fd) < 0)
    return -1;
  if (argaddr(1, &addr) < 0)
    return -1;
  if (argint(2, &n) < 0)
    return -1;

  if (fd != 1)
    return -1;                       // only the console write is in Lab 5 scope
  if (n <= 0)
    return 0;

  if (n > (int)sizeof(buf))
    n = sizeof(buf);

  if (copyin(p->pagetable, buf, addr, (uint64)n) < 0)
    return -1;

  {
    int i;
    for (i = 0; i < n; i++)
      consoleputc(buf[i]);
  }
  return n;
}