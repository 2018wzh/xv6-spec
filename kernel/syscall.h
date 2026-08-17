// syscall.h - user-mode system call numbers for the Lab 5 syscall surface.
//
// The first user process (and any later Lab 5 user code) numbers an ecall
// with the value placed in trap-frame a7 before trapping. Only the numbers
// in this table may be dispatched; any other a7 value is rejected by the
// bounds check in kernel/syscall.c and yields -1.

#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
#define SYS_read    5
#define SYS_kill    6
#define SYS_exec    7
#define SYS_fstat   8
#define SYS_chdir   9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_sleep  13
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21

// Highest valid syscall number. The dispatch table is indexed by a7 and
// checked against this bound before any table access.
#define SYS_MAX     21
