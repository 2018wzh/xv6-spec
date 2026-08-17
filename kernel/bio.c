// bio.c - The fixed LRU buffer cache (kernel/bio).
//
// Each device/block pair is represented by at most one cache entry, which
// gives upper layers a unique locked buffer for every active block
// (one-buffer-per-block). A doubly-linked LRU list is guarded by a single
// cache spinlock, and each buffer carries a per-buffer sleep lock that
// protects its contents. Lookup or reassignment of one cache identity is
// atomic under the cache lock; waiting for a buffer's sleep lock never
// happens while the cache spinlock is held, and no device wait occurs while
// the cache spinlock is held (forbidden_patterns).
//
// bread returns exactly one locked valid identity; brelse ends that caller
// ownership. A buffer that is referenced (refcnt > 0) or pinned cannot be
// reassigned, and only a zero-reference unpinned entry can be evicted
// (active-buffer-not-evicted / cache-reference-nonnegative). Log pinning
// (bpin) may outlive brelse and remains until checkpoint calls bunpin
// exactly once (bio-pin-lifetime-contract).

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "buf.h"

// The fixed cache: NBUF entries plus the LRU sentinel head, all guarded by
// one cache spinlock (`bcache.lock`). The head is not a usable buffer; it
// is the anchor of the doubly-linked LRU list.
struct bcache {
  struct spinlock lock;      // guards the LRU list, identities, refcnt.
  struct buf buf[NBUF];      // the fixed cache entries.
  struct buf head;           // LRU sentinel (not a usable buffer).
};
struct bcache bcache;

// Initialize the cache: one lock and a doubly-linked LRU list of NBUF
// entries in a fixed order, each with its per-buffer sleep lock. Runs once
// on the boot hart before any bread/bwrite/bpin call (precondition: virtio
// block I/O is initialized).
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create a doubly-linked LRU list anchored at head: head <-> buf[0] <->
  // buf[1] <-> ... <-> buf[NBUF-1] <-> head.
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look up a cache entry for (dev, blockno). If present, take one reference
// and return it locked; otherwise recycle the least recently used entry that
// is unreferenced and unpinned, reassign its identity, and return it locked.
// If the block is not cached and no entry may be evicted, panic rather than
// alias an active block (errors: exhaustion). Caller must later brelse it.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached? Every active identity is unique, so a
  // matching entry is the sole representation (cache-identity-unique).
  for (b = bcache.head.next; b != &bcache.head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached: recycle the least recently used, unreferenced, unpinned
  // entry. Eviction only ever selects a zero-reference unpinned entry
  // (active-buffer-not-evicted).
  for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
    if (b->refcnt == 0 && !b->pinned) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  panic("bget: no buffers");
  return 0;  // unreachable; panic never returns.
}

// Return a locked buffer for the given (dev, blockno), reading it from the
// device on first access. Returns exactly one locked valid identity; the
// caller must brelse it when done.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid) {
    // Read the complete 1024-byte logical block while holding the buffer's
    // sleep lock; virtio completes the transfer before returning.
    virtio_disk_rw(blockno, b->data, 0);
    b->valid = 1;
  }
  return b;
}

// Write back the buffer's current contents to the device. The caller must
// hold the buffer's sleep lock; the lock and caller reference are NOT
// released here (bio-write-contract).
void
bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->blockno, b->data, 1);
}

// Release a caller's reference: drop the buffer's sleep lock and the caller
// reference. Only a zero-reference unpinned entry becomes an eviction
// candidate; the entry is moved to the LRU tail (most recently unused) so
// it is evicted last among free entries. Panics if the caller does not hold
// the lock or own a live reference (errors: invalid ownership).
void
brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // No one references this buffer: move it to the tail of the LRU list
    // (the most recently unused end). The tail is the last entry whose next
    // link wraps to the sentinel head. If b is already the tail, it is
    // already in the correct position, so do not re-link it onto itself.
    if (b->next != &bcache.head) {
      // Unlink b from its current LRU position.
      b->prev->next = b->next;
      b->next->prev = b->prev;
      // Re-insert b as the new tail (immediately before the sentinel head):
      // its next link wraps to head and the previous tail points to it.
      b->prev = bcache.head.prev;
      bcache.head.prev->next = b;
      b->next = &bcache.head;
      bcache.head.prev = b;
    }
  }
  release(&bcache.lock);
}

// Increment the independent transaction pin reference. A pinned buffer
// cannot be evicted until checkpoint calls bunpin exactly once; pinning does
// not release the caller's buffer lock or ordinary reference
// (bio-pin-lifetime-contract).
void
bpin(struct buf *b)
{
  acquire(&bcache.lock);
  b->pinned++;
  release(&bcache.lock);
}

// Decrement the independent transaction pin reference. A double unpin or a
// reference that would become negative panics (errors: negative references,
// cache-reference-nonnegative).
void
bunpin(struct buf *b)
{
  acquire(&bcache.lock);
  if (b->pinned <= 0)
    panic("bunpin");
  b->pinned--;
  release(&bcache.lock);
}

// ---- per-buffer sleep lock implementation (kernel/bio) ----

// Initialize one per-buffer sleep lock.
void
initsleeplock(struct sleeplock *lk, const char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

// Acquire the buffer's sleep lock. Blocks (sleeps) on the lock rather than
// spinning, so the cache spinlock is never held while waiting
// (forbidden_patterns / interrupt_rules).
void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  while (lk->locked) {
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk);
}

// Release the buffer's sleep lock and wake one waiter.
void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}

// Return nonzero if the current process holds this buffer's sleep lock.
int
holdingsleep(struct sleeplock *lk)
{
  int r;

  acquire(&lk->lk);
  r = lk->locked && (lk->pid == (uint)myproc()->pid);
  release(&lk->lk);
  return r;
}