#ifndef __TYPES_H__
#define __TYPES_H__

typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
/* uint64 must be a genuine 64-bit type on every host that compiles kernel
 * headers. `unsigned long` is 32-bit under the Windows LLP64 data model
 * (mingw/gcc host compiles of kernel source and mkfs), so use the fixed-width
 * 64-bit `unsigned long long`, which is 64-bit on both RISC-V kernels and
 * POSIX/Windows hosts. */
typedef unsigned long long uint64;

typedef uint64 pte_t;
typedef uint64 *pagetable_t;
typedef uint64 pde_t;

// The register save area established by kernelvec.S. Each field corresponds
// to one symmetric 8-byte stack slot in kernelvec's full 32-register frame.
struct trapframe {
  uint64 ra;
  uint64 sp;
  uint64 gp;
  uint64 tp;
  uint64 t0;
  uint64 t1;
  uint64 t2;
  uint64 s0;
  uint64 s1;
  uint64 a0;
  uint64 a1;
  uint64 a2;
  uint64 a3;
  uint64 a4;
  uint64 a5;
  uint64 a6;
  uint64 a7;
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
  uint64 t3;
  uint64 t4;
  uint64 t5;
  uint64 t6;
};

#endif // __TYPES_H__
