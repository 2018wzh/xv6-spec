// file.h - the Lab 6 global file table and per-file operation surface
// (kernel/file).
//
// kernel/file owns the global file table, per-file offsets and references,
// validated user file syscalls, and the bounded user workload that exercises
// the Lab 6 file ABI. The table is a fixed NFILE array guarded by one file-
// table spinlock. Each populated process descriptor slot owns exactly one
// global file reference; each live inode-backed global file owns one balanced
// inode reference until its final close.
//
// Every successful open publishes one initialized reference; close clears the
// descriptor before releasing the final underlying inode reference exactly
// once. Per-inode sleep locks protect file contents (the table spinlock is
// never held across a persistent wait).

#ifndef __FILE_H__
#define __FILE_H__

#include "types.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"

// Directions for file operations (validation rejects anything else).
#define FD_NONE   0
#define FD_INODE  1
#define FD_PIPE   2

struct pipe;  // defined in kernel/pipe.c; owned by kernel/pipe.

struct file {
  int type;            // FD_INODE, FD_PIPE, or FD_NONE.
  int ref;             // number of live references (global table).
  char readable;       // 1 if the file is readable.
  char writable;       // 1 if the file is writable.
  struct inode *ip;    // FD_INODE: the underlying inode.
  struct pipe *pipe;   // FD_PIPE: the anonymous pipe endpoint.
  uint off;            // shared per-file offset (serialized by transfer).
};

// kernel/file owned operation surface.
void         fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *);
void         fileclose(struct file *);
int          filestat(struct file *, uint64 addr);
int          fileread(struct file *, uint64 addr, int n);
int          filewrite(struct file *, uint64 addr, int n);

// The fixed global file table (defined in file.c). One spinlock guards
// reference changes and identity selection; per-inode sleep locks protect
// contents, so the table lock is never held across a persistent wait.
struct filetable {
  struct spinlock lock;
  struct file file[NFILE];
};
extern struct filetable ftable;

#endif // __FILE_H__
