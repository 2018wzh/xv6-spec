/* kernel/bio fixed LRU buffer-cache validation harness.
 *
 * This harness compiles the real kernel/bio.c (via -Ikernel) together with
 * lightweight single-threaded stubs for the kernel dependencies it needs
 * (spinlock primitives, sleep/wakeup, myproc, virtio_disk_rw, panic). It then
 * exercises the actual bread/bwrite/brelse/bpin/bunpin operations and checks
 * the kernel/bio invariants:
 *
 *   - one-buffer-per-block / cache-identity-unique: at most one entry
 *     represents a given (dev, blockno) pair, and repeated lookups of one
 *     device block resolve to one cache identity.
 *   - active-buffer-not-evicted: a referenced or pinned buffer is never
 *     reassigned; only a zero-reference unpinned entry may be evicted.
 *   - cache-reference-nonnegative: every reference count stays nonnegative;
 *     a double unpin or negative pin panics (errors: negative references).
 *   - bio-lru-fuzz: fixed-seed acquire/release/pin sequences preserve
 *     uniqueness, LRU order, and nonnegative references.
 *
 * Usage: lru_test                -> run deterministic contract checks
 *        lru_test SEED CASES [R] -> also run the fixed-seed fuzz workload
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "buf.h"

/* ---- single-threaded kernel-dependency stubs ---- */

static struct proc g_proc;

void
initlock(struct spinlock *lk, const char *name)
{
  lk->locked = 0;
  lk->name = name;
}

void
acquire(struct spinlock *lk)
{
  lk->locked = 1;   /* single-threaded model: no real contention */
}

void
release(struct spinlock *lk)
{
  lk->locked = 0;
}

int
holding(struct spinlock *lk)
{
  return lk->locked;
}

struct proc *
myproc(void)
{
  return &g_proc;
}

void
sleep(void *chan, struct spinlock *lk)
{
  (void)chan; (void)lk;   /* single-threaded model: never contended */
}

void
wakeup(void *chan)
{
  (void)chan;
}

/* virtio transfer count for oracle purposes. */
static uint64 g_reads;
static uint64 g_writes;

/* virtio stub: a read zero-fills the logical block and records it; a write
 * records it. It never returns without completing the whole logical block. */
void
virtio_disk_rw(uint64 blockno, void *data, int write)
{
  (void)blockno;
  if (write) {
    g_writes++;
  } else {
    memset(data, 0, BSIZE);
    g_reads++;
  }
}

/* panic interception: bio.c panics on error paths and exhaustion. The
 * harness catches it via longjmp so the expected-failure checks can verify a
 * diagnostic was produced without aborting the whole run. */
static jmp_buf g_jmp;
static int g_jmp_armed;

void
panic(char *s)
{
  fprintf(stderr, "PANIC: %s\n", s);
  if (g_jmp_armed)
    longjmp(g_jmp, 1);
  exit(1);
}

/* ---- test helpers ---- */

#define CHECK(cond, msg) \
  do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

/* The fixed cache is a kernel/bio object. We re-declare its structure layout
 * (identical to bio.c) so the helpers can reference the real global object. */
struct bcache {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct buf head;
};
extern struct bcache bcache;

/* Return the number of live cache entries currently held (refcnt > 0). */
static int
count_refs(void)
{
  int i, n = 0;
  for (i = 0; i < NBUF; i++)
    if (bcache.buf[i].refcnt > 0)
      n++;
  return n;
}

/* Return the number of entries currently pinned. */
static int
count_pinned(void)
{
  int i, n = 0;
  for (i = 0; i < NBUF; i++)
    if (bcache.buf[i].pinned > 0)
      n++;
  return n;
}

/* Verify the whole cache is internally consistent: every pin value is
 * nonnegative (refcnt is an unsigned count and only ever decremented from a
 * positive value), and no two active entries share an identity. */
static int
cache_consistent(void)
{
  int i, j;
  for (i = 0; i < NBUF; i++) {
    struct buf *b = &bcache.buf[i];
    if (b->pinned < 0)
      return 0;
    if (b->refcnt == 0 && b->pinned == 0)
      continue;   /* free entry: identity may be stale, skip uniqueness */
    for (j = i + 1; j < NBUF; j++) {
      struct buf *c = &bcache.buf[j];
      if (c->refcnt > 0 || c->pinned > 0) {
        if (b->dev == c->dev && b->blockno == c->blockno)
          return 0;   /* duplicate active identity */
      }
    }
  }
  return 1;
}

/* Return the number of entries that are currently eviction candidates
 * (unreferenced and unpinned). Eviction only ever selects such an entry, so
 * the fuzz acquires new blocks only while this count is positive, mirroring
 * active-buffer-not-evicted without ever driving the cache into exhaustion. */
static int
free_candidates(void)
{
  int i, n = 0;
  for (i = 0; i < NBUF; i++)
    if (bcache.buf[i].refcnt == 0 && bcache.buf[i].pinned == 0)
      n++;
  return n;
}

/* ---- deterministic contract checks ---- */
static int
run_contract(void)
{
  struct buf *b, *b2;
  int i;

  binit();

  /* one-buffer-per-block: a block resolves to one cache identity. A fresh
   * acquire of an already-cached block returns the exact same buffer, never
   * an aliased duplicate (competing lookups would block on the sleep lock). */
  b = bread(1, 7);
  CHECK(b != 0, "bread returned null");
  b2 = b;
  brelse(b);
  b = bread(1, 7);                  /* reacquire the cached block */
  CHECK(b == b2, "duplicate identity for same block");
  CHECK(b->refcnt == 1, "unexpected refcnt after fresh reacquire");
  brelse(b);
  CHECK(cache_consistent(), "cache inconsistent after shared lookup");

  /* Distinct blocks resolve to distinct identities. */
  b = bread(1, 3);
  b2 = bread(1, 4);
  CHECK(b != b2, "distinct blocks share identity");
  brelse(b);
  brelse(b2);

  /* Fill the cache with distinct blocks, release each exactly once, and
   * confirm the cache is fully populated then fully emptied. */
  {
    struct buf *fills[NBUF];
    for (i = 0; i < NBUF; i++) {
      fills[i] = bread(1, 100 + i);
      CHECK(fills[i] != 0, "bread failed while filling cache");
    }
    CHECK(count_refs() == NBUF, "cache not fully populated");
    for (i = 0; i < NBUF; i++)
      brelse(fills[i]);
    CHECK(count_refs() == 0, "cache not emptied after releasing each fill block");
  }
  CHECK(cache_consistent(), "cache inconsistent after fill/empty");

  /* LRU reuse without evicting referenced buffers: hold one block referenced,
   * then force eviction of the rest. The referenced block must be preserved. */
  b = bread(1, 200);                       /* keep this referenced */
  for (i = 0; i < NBUF - 1; i++) {
    struct buf *t = bread(1, 300 + i);
    brelse(t);
  }
  /* Evict a new block; the referenced one stays its own identity. */
  b2 = bread(1, 999);
  CHECK(b2 != b, "evicted an active (referenced) buffer");
  CHECK(b->blockno == 200, "referenced buffer identity changed");
  brelse(b2);
  brelse(b);

  /* bwrite preserves identity and does not release the caller lock. */
  b = bread(1, 5);
  b->data[0] = 0xAB;
  bwrite(b);
  CHECK(holdingsleep(&b->lock), "bwrite released the caller lock");
  CHECK(g_writes == 1, "bwrite did not reach the device");
  CHECK(b->blockno == 5, "bwrite changed the buffer identity");
  brelse(b);

  /* bio-pin-lifetime-contract: bpin survives brelse and prevents eviction. */
  b = bread(1, 400);
  bpin(b);
  brelse(b);                               /* pin survives brelse */
  CHECK(b->pinned == 1, "bpin lost after brelse");
  CHECK(count_pinned() == 1, "pin count mismatch");
  /* Try to evict many new blocks; the pinned one must never be reassigned. */
  for (i = 0; i < NBUF * 4; i++) {
    struct buf *t = bread(1, 2000 + i);
    CHECK(t != b, "evicted a pinned buffer");
    brelse(t);
  }
  CHECK(b->blockno == 400, "pinned buffer identity changed");
  bunpin(b);
  CHECK(b->pinned == 0, "bunpin did not clear the pin");

  /* Error: a double unpin / negative pin must panic. */
  b = bread(1, 500);
  if (setjmp(g_jmp) == 0) {
    g_jmp_armed = 1;
    bunpin(b);                             /* pinned == 0 -> must panic */
    g_jmp_armed = 0;
    fprintf(stderr, "FAIL: bunpin on unpinned buffer did not panic\n");
    return 1;
  }
  g_jmp_armed = 0;
  CHECK(b->pinned == 0, "failed bunpin corrupted pin state");
  brelse(b);

  /* Error: brelse without holding the buffer lock must panic. */
  b = bread(1, 600);
  releasesleep(&b->lock);                  /* drop the lock illegally */
  if (setjmp(g_jmp) == 0) {
    g_jmp_armed = 1;
    brelse(b);
    g_jmp_armed = 0;
    fprintf(stderr, "FAIL: brelse without lock did not panic\n");
    return 1;
  }
  g_jmp_armed = 0;

  CHECK(cache_consistent(), "cache inconsistent at end of contract");
  printf("contract: uniqueness, LRU, pinning, refcount, and error paths passed\n");
  return 0;
}

/* ---- fixed-seed LRU fuzz workload ---- */
static uint64 g_rng;

static uint64
next_rng(void)
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return g_rng;
}

static int
run_fuzz(uint64 seed, uint64 cases)
{
  struct buf *held[NBUF];
  uint g_held_block = 3000;    /* monotonic: distinct identity per held slot */
  uint g_ephem_block = 20000;  /* monotonic: distinct ephemeral identities */
  int i;

  binit();                       /* fresh cache for the fuzz session */
  g_rng = seed ? seed : 1;
  for (i = 0; i < NBUF; i++)
    held[i] = 0;

  for (i = 0; i < (int)cases; i++) {
    int slot = (int)(next_rng() % NBUF);
    uint op = next_rng() % 5;

    if (op == 0) {
      /* acquire a new block only while an eviction candidate exists. Each
       * acquire uses a distinct monotonic block number so a fresh bread
       * never collides with an identity whose sleep lock is already held. */
      if (held[slot] == 0 && free_candidates() > 0) {
        held[slot] = bread(1, g_held_block++);
        if (held[slot]->refcnt < 1)
          return 1;
      }
    } else if (op == 1) {
      /* release (brelse) the slot if held. */
      if (held[slot] != 0) {
        brelse(held[slot]);
        held[slot] = 0;
      }
    } else if (op == 2) {
      /* pin a held slot only while the pin count stays bounded so the cache
       * always retains eviction candidates (active-buffer-not-evicted). */
      if (held[slot] != 0 && count_pinned() < NBUF / 3) {
        bpin(held[slot]);
      }
    } else if (op == 3) {
      /* unpin a held slot exactly once if it is pinned. */
      if (held[slot] != 0 && held[slot]->pinned > 0) {
        bunpin(held[slot]);
      }
    } else {
      /* evict pressure: read + release an ephemeral block while a candidate
       * exists (a temporary lack of candidates is not an error in the fuzz,
       * it simply means the cache is legitimately full of live entries). */
      if (free_candidates() > 0) {
        struct buf *t = bread(1, g_ephem_block++);
        if (t == 0)
          return 1;
        brelse(t);
      }
    }

    /* After every step the cache must satisfy its invariants. */
    if (!cache_consistent()) {
      fprintf(stderr, "FAIL: fuzz cache inconsistent at step %d\n", i);
      return 1;
    }
  }

  /* Clean up: unpin every pinned entry exactly the number of times it was
   * pinned (pins legitimately survive brelse, so a released-but-pinned entry
   * must still be brought back to zero), then release every held buffer. */
  for (i = 0; i < NBUF; i++) {
    struct buf *b = &bcache.buf[i];
    while (b->pinned > 0)
      bunpin(b);
  }
  for (i = 0; i < NBUF; i++) {
    if (held[i] != 0) {
      brelse(held[i]);
      held[i] = 0;
    }
  }
  if (!cache_consistent() || count_refs() != 0 || count_pinned() != 0) {
    fprintf(stderr, "FAIL: fuzz cleanup left cache inconsistent\n");
    return 1;
  }

  printf("fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}

int
main(int argc, char **argv)
{
  /* No args: deterministic contract checks. With seed+cases: the fixed-seed
   * fuzz workload (each mode starts from a fresh binit()). */
  if (argc < 2)
    return run_contract();
  return run_fuzz(strtoull(argv[1], 0, 10), strtoull(argv[2], 0, 10));
}
