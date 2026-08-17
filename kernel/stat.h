// stat.h - user-visible file status shape for the Lab 6 file ABI
// (kernel/file).
//
// kernel/file owns the user-facing stat header. The kernel-side canonical
// `struct stat` lives in kernel/fs.h (owned by kernel/inode, filled by the
// inode `stati` operation); this header provides the equivalent user-facing
// definition and file-type constants that user programs include. User code
// never redefines or dereferences kernel structures.

#ifndef __STAT_H__
#define __STAT_H__

#include "types.h"

#define T_DIR     1   // Directory
#define T_FILE    2   // Regular file
#define T_DEVICE  3   // Device

struct stat {
  int dev;     // File system's device number.
  uint ino;    // Inode number.
  short type;  // Type of file (T_DIR/T_FILE/T_DEVICE).
  short nlink; // Number of hard links.
  uint64 size; // Size of file in bytes.
};

#endif // __STAT_H__
