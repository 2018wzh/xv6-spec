// user.h - Lab 6 user-side syscall declarations (kernel/file).
//
// The bounded user workload that exercises the Lab 6 file ABI calls the
// kernel through the validated ecall boundary. Only the syscall numbers in
// kernel/syscall.h are valid; each wrapper places the number in a7 and the
// arguments in a0-a5 before ecall, using inline assembly so no libc is
// required. This header is compiled only by the user programs under user/.

#ifndef __USER_H__
#define __USER_H__

#include "syscall.h"
#include "stat.h"
#include "fcntl.h"

static inline __attribute__((always_inline)) long
syscall0(int num)
{
  register long a7 asm("a7") = num;
  register long a0 asm("a0");
  asm volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
  return a0;
}

static inline __attribute__((always_inline)) long
syscall1(int num, long a0v)
{
  register long a7 asm("a7") = num;
  register long a0 asm("a0") = a0v;
  asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
  return a0;
}

static inline __attribute__((always_inline)) long
syscall2(int num, long a0v, long a1v)
{
  register long a7 asm("a7") = num;
  register long a0 asm("a0") = a0v;
  register long a1 asm("a1") = a1v;
  asm volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1) : "memory");
  return a0;
}

static inline __attribute__((always_inline)) long
syscall3(int num, long a0v, long a1v, long a2v)
{
  register long a7 asm("a7") = num;
  register long a0 asm("a0") = a0v;
  register long a1 asm("a1") = a1v;
  register long a2 asm("a2") = a2v;
  asm volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
  return a0;
}

static inline int open(const char *path, int flags)
{
  return (int)syscall2(SYS_open, (long)path, (long)flags);
}

static inline int write(int fd, const void *buf, int n)
{
  return (int)syscall3(SYS_write, fd, (long)buf, n);
}

static inline int read(int fd, void *buf, int n)
{
  return (int)syscall3(SYS_read, fd, (long)buf, n);
}

static inline int close(int fd)
{
  return (int)syscall1(SYS_close, fd);
}

static inline int dup(int fd)
{
  return (int)syscall1(SYS_dup, fd);
}

static inline int mkdir(const char *path)
{
  return (int)syscall1(SYS_mkdir, (long)path);
}

static inline int chdir(const char *path)
{
  return (int)syscall1(SYS_chdir, (long)path);
}

#endif // __USER_H__