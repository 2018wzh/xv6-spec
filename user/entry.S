/* user/entry.S - the user entry stub for the Lab 6 user workload. The user
 * programs enter at _start, call main(), and then spin forever (no exit
 * system call is in the Lab 6 file ABI; the scheduler keeps the boot hart
 * live). Assembled with the RISC-V toolchain; only used by the optional
 * user-program build target in the Makefile. */

  .globl _start
_start:
  call main
1:
  j 1b