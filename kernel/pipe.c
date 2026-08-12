// pipe.c - bounded anonymous pipes and endpoint lifetime (kernel/pipe).
//
// kernel/pipe owns one bounded byte ring with monotonic read and write
// positions and readable/writable endpoint liveness protected by one pipe
// lock. pipealloc publishes two initialized file references only after the
// pipe and both endpoints exist; the final reference on each side publishes
// closure and wakes peers; the backing pipe is freed only after both sides
// are closed.
//
// piperead and pipewrite exchange each byte under one pipe-lock critical
// section and block only through the process lock-handoff sleep/wakeup
// primitive (kernel/process). Every wait rechecks its condition after
// wakeup; endpoint close wakes both read-position and write-position wait
// channels so no waiter is left asleep after the condition it awaits becomes
// impossible because its final peer closed.
//
// The descriptor and file-table references are stabilized by kernel/file
// before the pipe lock is acquired, and the file-table lock is never
// retained while awaiting the pipe lock or sleeping.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "buf.h"      /* defines struct sleeplock before fs.h uses it. */
#include "proc.h"
#include "file.h"
#include "defs.h"

// Fixed ring capacity for the byte buffer (a small, bounded pipe).
#define PIPESIZE 512

// One anonymous pipe. nread and nwrite are monotonic positions; buffered
// bytes are data[nread % PIPESIZE .. nwrite % PIPESIZE). readopen and
// writeopen record whether at least one live file reference owns the
// corresponding endpoint side.
struct pipe {
  struct spinlock lock;
  char data[PIPESIZE];
  uint nread;     // bytes read so far (monotonic).
  uint nwrite;    // bytes written so far (monotonic).
  int readopen;   // a readable endpoint reference is live.
  int writeopen;  // a writable endpoint reference is live.
};

// p = pipealloc(f0, f1): allocate one initialized empty anonymous pipe plus
// two global file objects owning opposite references to it. On success writes
// the readable file into *f0 and the writable file into *f1 and returns 0.
// On any failure releases every provisional allocation and returns -1
// without leaving *f0 or *f1 non-null.
int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi = 0;
  struct file *rf = 0, *wf = 0;

  *f0 = *f1 = 0;

  if ((rf = filealloc()) == 0 || (wf = filealloc()) == 0)
    goto bad;
  if ((pi = (struct pipe *)kalloc()) == 0)
    goto bad;

  pi->readopen = 1;
  pi->writeopen = 1;
  pi->nread = 0;
  pi->nwrite = 0;
  initlock(&pi->lock, "pipe");

  // The readable endpoint: file type FD_PIPE, direction read-only.
  rf->type = FD_PIPE;
  rf->readable = 1;
  rf->writable = 0;
  rf->pipe = pi;

  // The writable endpoint: file type FD_PIPE, direction write-only.
  wf->type = FD_PIPE;
  wf->readable = 0;
  wf->writable = 1;
  wf->pipe = pi;

  *f0 = rf;
  *f1 = wf;
  return 0;

bad:
  if (pi)
    kfree((void *)pi);
  if (rf)
    fileclose(rf);
  if (wf)
    fileclose(wf);
  return -1;
}

// Release one endpoint reference of pipe `pi`. `writable` is 1 when closing
// the writable side, 0 when closing the readable side. Publishes closure
// under the pipe lock, wakes both read-position and write-position wait
// channels, and frees the backing pipe only after both sides have no live
// reference.
void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  if (writable) {
    pi->writeopen = 0;
    wakeup(&pi->nread);
    wakeup(&pi->nwrite);
  } else {
    pi->readopen = 0;
    wakeup(&pi->nread);
    wakeup(&pi->nwrite);
  }
  if (pi->readopen == 0 && pi->writeopen == 0) {
    // Both sides closed; free the backing pipe only after releasing the lock
    // so no waiter retains a pointer across the free.
    release(&pi->lock);
    kfree((void *)pi);
  } else {
    release(&pi->lock);
  }
}

// Copy `n` bytes from user address `addr` into the pipe in FIFO order,
// blocking only while the ring is full and at least one reader remains live.
// Returns the number of bytes accepted (0 only when no byte could be
// accepted), or -1 when the read side is permanently closed, the user range
// is invalid, or no progress is possible.
int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while (i < n) {
    if (pi->readopen == 0) {
      // The read side is permanently closed: a blocked writer observes a
      // stable broken-pipe failure instead of sleeping forever.
      release(&pi->lock);
      return -1;
    }
    if (pi->nwrite == pi->nread + PIPESIZE) {
      // Ring full: publish the potential-wake condition for readers and wait
      // on the write position. No lock is retained while sleeping.
      wakeup(&pi->nread);
      sleep(&pi->nwrite, &pi->lock);
    } else {
      char ch;
      if (copyin(pr->pagetable, &ch, addr + i, 1) == -1) {
        if (i == 0) {
          release(&pi->lock);
          return -1;   // invalid user range: no byte accepted, report failure.
        }
        break;   // partial write: report the accepted count.
      }
      pi->data[pi->nwrite++ % PIPESIZE] = ch;
      i++;
    }
  }
  wakeup(&pi->nread);
  release(&pi->lock);
  return i;
}

// Copy up to `n` bytes from the pipe into user address `addr` in FIFO order,
// blocking while the ring is empty and at least one writer remains live.
// Returns the number of bytes read (0 at EOF when no writer is left), or -1
// on an invalid user range.
int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while (pi->nread == pi->nwrite && pi->writeopen) {
    // Ring empty but a writer may still produce: wait on the read position.
    sleep(&pi->nread, &pi->lock);
  }
  for (i = 0; i < n; i++) {
    if (pi->nread == pi->nwrite)
      break;   // ring drained (or EOF): no more bytes to expose.
    {
      char ch = pi->data[pi->nread % PIPESIZE];
      if (copyout(pr->pagetable, addr + i, &ch, 1) == -1) {
        if (i == 0) {
          release(&pi->lock);
          return -1;   // invalid user range: no byte consumed, report failure.
        }
        break;   // partial read: bytes up to i were delivered in order.
      }
      pi->nread++;   // advance only after the byte is delivered.
    }
  }
  wakeup(&pi->nwrite);
  release(&pi->lock);
  return i;
}