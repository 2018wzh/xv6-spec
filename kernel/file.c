// file.c - the Lab 6 global file table and per-file operation semantics
// (kernel/file).
//
// kernel/file owns the global file table, per-file offsets and reference
// state, and the validated file-operation semantics aggregated by sysfile.c.
// Each populated process descriptor slot owns exactly one global file
// reference; each live inode-backed file owns one balanced inode reference
// released only by final close.
//
// The table is guarded by a file-table spinlock. File contents live in the
// underlying inode whose per-inode sleep lock serializes offset changes with
// the successful byte transfer. The file-table spinlock is released before
// any inode, buffer, or transaction-capacity wait, so no file-table spinlock
// is held across a persistent wait.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "buf.h"
#include "fs.h"
#include "file.h"
#include "defs.h"

// The fixed global file table (declared in file.h).
struct filetable ftable;

// Initialize the global file table. Each slot starts with zero live
// references so no slot owns an inode reference before a file is opened.
void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure and return it with ref == 1. Returns 0 when the
// table is exhausted (file_capacity) without reinitializing any live slot.
struct file *
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for (f = ftable.file; f < ftable.file + NFILE; f++) {
    if (f->ref == 0) {
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;   // file-table exhaustion.
}

// Increment the reference count of a file structure and return the same
// pointer. Called when a process descriptor is duplicated (dup) so two
// descriptors share one global file with a serialized offset.
struct file *
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close a file: release its final reference exactly once. On the final close
// (ref -> 0), releases the underlying inode reference. Must never hold the
// file-table spinlock across the inode/buffer/transaction waits.
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("fileclose");
  if (--f->ref > 0) {
    release(&ftable.lock);
    return;
  }
  ff = *f;         // copy the mutable fields out while holding the table lock.
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if (ff.type == FD_INODE) {
    begin_op();
    iput(ff.ip);
    end_op();
  } else if (ff.type == FD_PIPE) {
    // Release one endpoint reference of the anonymous pipe. The readable
    // side is closed when this file was writable (the peer of a reader);
    // the writable side is closed when this file was readable.
    pipeclose(ff.pipe, ff.writable);
  }
}

// Return the stat of an open file into the user address `addr` by aggregating
// kernel/inode.stati through the validated copyout boundary. Returns 0 on
// success, -1 on an invalid direction or user copy failure.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;

  if (f->type != FD_INODE)
    return -1;

  ilock(f->ip);
  stati(f->ip, &st);
  iunlock(f->ip);
  if (copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}

// Read into the user buffer at `addr` from the shared file offset using the
// inode's validated either-copy path. Returns the number of bytes read (0 at
// EOF) or -1 on an invalid descriptor/direction.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if (!f->readable)
    return -1;
  if (f->type == FD_PIPE) {
    // Forward to the bounded anonymous-pipe read (FIFO, blocking on empty
    // with a live writer; EOF when the write side is permanently closed).
    return piperead(f->pipe, addr, n);
  }
  if (f->type != FD_INODE || f->ip == 0 || f->ip->type == T_DEVICE)
    return -1;   // device-backed reads are outside the Lab 6 file ABI.

  ilock(f->ip);
  r = readi(f->ip, 1, addr, f->off, n);
  if (r > 0)
    f->off += r;   // the shared offset advances by exactly the transferred count.
  iunlock(f->ip);
  return r;
}

// Write n bytes from the user buffer at `addr` into the shared file offset
// using the inode's validated either-copy path inside redo transactions.
// Returns the number of bytes written or -1 on an invalid descriptor.
int
filewrite(struct file *f, uint64 src, int n)
{
  int r, ret = 0, max = ((MAXOPBLOCKS - 1) / 2) * BSIZE;

  if (!f->writable)
    return -1;
  if (f->type == FD_PIPE) {
    // Forward to the bounded anonymous-pipe write (FIFO, blocking on full
    // with a live reader; stable broken-pipe failure on a closed read side).
    return pipewrite(f->pipe, src, n);
  }
  if (f->type != FD_INODE || f->ip == 0 || f->ip->type == T_DEVICE)
    return -1;   // device-backed writes are outside the Lab 6 file ABI.

  ilock(f->ip);
  if (f->off > f->ip->size)
    f->off = f->ip->size;   // a write at a stale offset is bounded at EOF.

  while (n > 0) {
    if (n > max)
      n = max;   // bound each transaction to MAXOPBLOCKS worth of writes.
    begin_op();
    r = writei(f->ip, 1, src + ret, f->off, n);
    end_op();
    if (r < 0) {
      if (ret == 0)
        ret = -1;
      break;
    }
    if (r == 0)
      break;   // out of capacity: partial write is durable and explicit.
    ret += r;
    f->off += r;   // the shared offset advances by exactly the transferred count.
    src += r;
    n -= r;
  }
  iunlock(f->ip);
  return ret;
}
