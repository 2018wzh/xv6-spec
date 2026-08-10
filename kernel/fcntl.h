// fcntl.h - user-visible open flags for the Lab 6 file ABI (kernel/file).
//
// The `open` syscall validates the integer mode argument against these
// flags. Each successful open publishes exactly one initialized global file
// reference in one process descriptor slot; an invalid or exhausted mode
// returns -1 without publishing a reference. Defined on the user side
// (included from types.h) and the kernel side (sysfile.c) with identical
// numeric values so the validated ecall ABI is stable.

#ifndef __FCNTL_H__
#define __FCNTL_H__

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400

#endif // __FCNTL_H__
