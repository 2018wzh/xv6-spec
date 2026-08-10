// buf.h - The fixed LRU buffer-cache entry and its per-buffer sleep lock.
//
// kernel/bio owns a fixed set of NBUF cache entries. Each entry gives one
// device/block pair a single in-memory identity, coordinates locking,
// pinning, reads, and writeback, and is linked into a doubly-linked LRU list
// guarded by the cache spinlock. A per-buffer sleep lock protects contents;
// waiting for it never holds the cache spinlock (forbidden_patterns).

#ifndef __BUF_H__
#define __BUF_H__

#include "types.h"
#include "spinlock.h"

// Bytes per logical block. Each logical block maps to two adjacent 512-byte
// device sectors (kernel/virtio sector-range-partition).
#define BSIZE 1024

// A per-buffer sleep lock protecting the buffer's contents. `locked` records
// whether the content lock is held and which process owns it; the small `lk`
// spinlock protects that bookkeeping so contention waits on the buffer via
// sleep/wakeup rather than spinning.
struct sleeplock {
  uint locked;            // is the content lock held?
  struct spinlock lk;     // guards the locked/pid fields.
  const char *name;       // diagnostic name of the lock.
  uint pid;               // pid of the current holder (0 = none).
};

// A buffer-cache entry. `refcnt` is the number of live caller references;
// `pinned` is an independent transaction pin reference (>= 0) that survives
// brelse and prevents the entry from becoming an eviction candidate until
// the log checkpoint calls bunpin exactly once.
struct buf {
  int valid;              // have the contents been read from the device?
  int disk;               // is a device read/write in flight?
  uint dev;               // device number of this entry's identity.
  uint blockno;           // logical block number within the device.
  struct sleeplock lock;  // protects buffer contents.
  uint refcnt;            // number of caller references to this entry.
  int pinned;             // independent transaction pin reference (>=0).
  struct buf *prev;       // LRU: previous cache entry.
  struct buf *next;       // LRU: next cache entry.
  uchar data[BSIZE];      // the block contents.
};

// buffer-cache operations owned by kernel/bio.
void         binit(void);
struct buf*  bread(uint dev, uint blockno);
void         bwrite(struct buf*);
void         brelse(struct buf*);
void         bpin(struct buf*);
void         bunpin(struct buf*);

// per-buffer sleep lock helpers (kernel/bio owns their implementation).
void         initsleeplock(struct sleeplock*, const char*);
void         acquiresleep(struct sleeplock*);
void         releasesleep(struct sleeplock*);
int          holdingsleep(struct sleeplock*);

#endif // __BUF_H__