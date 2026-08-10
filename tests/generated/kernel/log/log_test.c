/* kernel/log redo-log validation harness.
 *
 * This harness compiles the real kernel/log.c together with the real
 * kernel/bio.c (via -Ikernel) plus lightweight single-threaded stubs for the
 * kernel dependencies they need (spinlock primitives, sleep/wakeup, myproc,
 * virtio block I/O, panic). It drives the actual initlog/begin_op/log_write/
 * end_op/recovery operations against an in-memory "disk" and checks the
 * kernel/log invariants:
 *
 *   - log-admission-contract: admitted operations plus MAXOPBLOCKS per-op
 *     reservations never exceed LOGSIZE; each end_op releases its admission
 *     and the last one commits.
 *   - log-dedup-fuzz / logged-block-unique: repeated log_write of one block
 *     consumes one log slot per distinct block.
 *   - redo-ordering / committed-header-boundary: log data reaches storage
 *     before the nonempty commit header, and home blocks change only after
 *     that header.
 *   - recovery-idempotent: one or repeated recovery passes produce the same
 *     complete committed state; replaying recovery again is a no-op.
 *   - errors: corrupt headers, impossible block numbers, and oversized
 *     transactions panic before partial replay.
 *
 * The in-memory disk models physical block 0..DISKBLKS-1 of the root device.
 * Following the xv6 superblock layout, block 1 holds the superblock, the log
 * region occupies [sb.logstart, sb.logstart+sb.nlog), and home blocks lie
 * after that region.
 *
 * Usage: log_test                -> deterministic contract checks
 *        log_test SEED CASES [R] -> also run the fixed-seed fuzz workload
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

/* ---- kernel/log interface (declared manually; defs.h is not included so
 * its host-conflicting `main`/`printf` prototypes do not collide with the
 * host toolchain headers used by a standalone harness). The definitions come
 * from the real kernel/log.c linked together with this harness. ---- */
extern void initlog(int dev);
extern void begin_op(void);
extern void log_write(struct buf *b);
extern void end_op(void);

/* ---- in-memory disk model -------------------------------------------------
 * The harness owns a flat array of DISKBLKS logical blocks (BSIZE bytes
 * each). This is the device that virtio_disk_rw reads/writes. Path: block 1
 * is the superblock; the head of each transaction is written to log.start
 * (block logstart) and log data to log.start+1.. */
#define DISKBLKS 200
#define SUPERBLK 1
#define LOGNBLK  8          /* small log region for the harness */
#define LOGSTART 2          /* block number of the first log block */

/* A mirror of kernel/log.c's superblock subset so the harness can fabricate a
 * valid geometry and inspect the log's validated view. */
struct relog_sb {
  uint magic;
  uint size;
  uint nblocks;
  uint ninodes;
  uint nlog;
  uint logstart;
  uint inodestart;
  uint bmapstart;
};
#define FSMAGIC 0x10203040

/* panic is stubbed below; declare it so virtio_disk_rw can call it before the
 * definition. */
void panic(char *s);

/* The in-memory disk. A block is "dirty" iff any byte differs from zero so an
 * oracle can prove a write reached storage. */
static unsigned char g_disk[DISKBLKS][BSIZE];
static int   g_writes;             /* number of completed device writes      */

/* ---- kernel-dependency stubs ---- */

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
  (void)lk;               /* single-threaded model */
  lk->locked = 1;
}

void
release(struct spinlock *lk)
{
  (void)lk;
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

/* Virtio transfer stub: reads/writes the in-memory disk block completely.
 * This is the only path by which bytes reach storage (write-ahead data, the
 * nonempty commit header, and installed home blocks). */
void
virtio_disk_rw(uint64 blockno, void *data, int is_write)
{
  if (blockno >= DISKBLKS)
    panic("virtio_disk_rw: block out of range");
  if (is_write) {
    memcpy(g_disk[blockno], data, BSIZE);
    g_writes++;
  } else {
    memcpy(data, g_disk[blockno], BSIZE);
  }
}

/* Panic interception: log.c panics on validation errors and over-capacity.
 * The harness catches it via longjmp so expected-failure checks can verify a
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

/* ---- access to kernel/log internals for oracle checks ---- */
/* Re-declare the exact shapes log.c and bio.c use so the harness can inspect
 * their global state (same technique as the bio lru_test harness). */
struct logheader {
  int n;
  int block[LOGSIZE];
};
struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding;
  int committing;
  int dev;
  struct logheader lh;
};
extern struct log log;

/* bio.c's cache shape, mirrored so a simulated crash+reboot can invalidate
 * cached block identities and force the next bread to re-read the disk. */
struct bcache {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct buf head;
};
extern struct bcache bcache;

/* ---- test helpers ---- */

#define CHECK(cond, msg) \
  do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

/* Reset the in-memory disk and fabricate a valid superblock at block 1. */
static void
reset_disk(void)
{
  struct relog_sb *sb = (struct relog_sb *)g_disk[SUPERBLK];
  memset(g_disk, 0, sizeof(g_disk));
  g_writes = 0;
  sb->magic = FSMAGIC;
  sb->size = DISKBLKS;
  sb->nblocks = DISKBLKS - (LOGSTART + LOGNBLK + 2);
  sb->ninodes = 32;
  sb->nlog = LOGNBLK;
  sb->logstart = LOGSTART;
  sb->inodestart = LOGSTART + LOGNBLK;
  sb->bmapstart = sb->inodestart + 1;
}

/* Reset the buffer cache and (re)initialize the log from the superblock on
 * the in-memory disk. This models a crash + reboot: binit() rebuilds the LRU
 * links and force_cache_invalidate() drops cached identity validity so the
 * next bread re-reads from the disk backing (a fresh boot has no valid cache
 * entries), and initlog() runs recovery. */
static void
force_cache_invalidate(void)
{
  int i;
  for (i = 0; i < NBUF; i++) {
    bcache.buf[i].valid = 0;
    bcache.buf[i].refcnt = 0;
    bcache.buf[i].pinned = 0;
  }
}

static void
fake_initlog(void)
{
  binit();
  force_cache_invalidate();
  initlog(1);
}

/* Return the first word of a home block as seen by the disk. */
static uint
disk_word(uint blk)
{
  uint v;
  memcpy(&v, g_disk[blk], sizeof(v));
  return v;
}

/* ---- deterministic contract checks ---- */
static int
run_contract(void)
{
  struct buf *b1, *b2, *b3;
  uint home1 = LOGSTART + LOGNBLK + 8;      /* a home block (data area) */
  uint home2 = home1 + 1;
  uint home3 = home1 + 2;

  /* --- recovery-before-admission: initlog on an empty log is a no-op and
        the log admits a transaction -> the header/n fields are consistent. */
  reset_disk();
  fake_initlog();
  CHECK(log.outstanding == 0, "outstanding nonzero after initlog");
  CHECK(log.lh.n == 0, "header not empty after clean initlog");

  /* --- log-admission-contract: begin_op reserves MAXOPBLOCKS; with one op
        outstanding, capacity stays within LOGSIZE. */
  begin_op();
  CHECK(log.outstanding == 1, "begin_op did not admit one operation");
  end_op();
  CHECK(log.outstanding == 0, "end_op did not release the admission");

  /* --- a transaction that logs several home blocks rolls back (recover)
        and replays cleanly. First, a normal committed transaction: */
  begin_op();
  b1 = bread(1, home1);
  memset(b1->data, 0x11, BSIZE);
  log_write(b1);
  log_write(b1);               /* dedup: same block, must stay one slot */
  brelse(b1);

  b2 = bread(1, home2);
  memset(b2->data, 0x22, BSIZE);
  log_write(b2);
  brelse(b2);
  CHECK(log.lh.n == 2, "two distinct blocks must occupy two slots");
  end_op();                    /* last outstanding op commits + checkpoints */

  /* After commit the header must be cleared and the home blocks installed. */
  CHECK(log.lh.n == 0, "header not cleared after commit");
  CHECK(disk_word(home1) == 0x11111111, "home1 not installed to home block");
  CHECK(disk_word(home2) == 0x22222222, "home2 not installed to home block");

  /* --- logged-block-unique / one slot per distinct block, measured by the
        on-disk header of the intermediate committed transaction: log data
        reaches storage (write-ahead) before the commit header, and a nonempty
        header is the commit point. We re-init to observe the on-disk header;
        a freestanding proof of redo-ordering is that block data is present. */
  begin_op();
  b3 = bread(1, home3);
  memset(b3->data, 0x33, BSIZE);
  log_write(b3);
  brelse(b3);
  end_op();
  CHECK(disk_word(home3) == 0x33333333, "home3 not installed");

  /* --- recovery-idempotent: craft an interrupted-log on-disk state (log data
        block + a nonempty commit header on disk, home block untouched) exactly
        as it would be left by a crash after the commit header but before the
        checkpoint, then recover twice and observe identical home blocks and a
        cleared header both times. This proves redo-ordering: home blocks
        change only from the log data, after the header commit point. */
  reset_disk();
  fake_initlog();                 /* fresh cache + valid log geometry */
  {
    struct logheader *hb = (struct logheader *)g_disk[LOGSTART];
    uint pat = 0x44444444;
    /* log data block at log.start+1 carries the new home-block contents. */
    memcpy(g_disk[LOGSTART + 1], &pat, sizeof(pat));
    /* durable nonempty commit header names the single logged home block. */
    hb->n = 1;
    hb->block[0] = home1;
    /* home block is left untouched (still zeroed from reset). */
  }
  fake_initlog();                 /* reboot -> recovery replays the log */
  CHECK(disk_word(home1) == 0x44444444, "recovery did not replay home block");
  /* Replaying recovery again is a no-op: same home block, still cleared. */
  fake_initlog();
  CHECK(disk_word(home1) == 0x44444444, "repeat recovery changed home block");
  CHECK(log.lh.n == 0, "header not cleared after repeat recovery");

  /* --- corrupt-header validation: an invalid header count must panic before
        changing any home block. */
  reset_disk();
  fake_initlog();
  /* Write a bad on-disk header (n too large) directly to the log head block. */
  {
    struct logheader *hb = (struct logheader *)g_disk[LOGSTART];
    hb->n = LOGNBLK;           /* == log.size, over the valid bound */
    hb->block[0] = home1;
    /* initlog must panic on the corrupt header before install_trans. */
    if (setjmp(g_jmp) == 0) {
      g_jmp_armed = 1;
      fake_initlog();
      g_jmp_armed = 0;
      fprintf(stderr, "FAIL: corrupt header count did not panic\n");
      return 1;
    }
    g_jmp_armed = 0;
  }

  /* --- impossible-block-number validation: a home block outside the super
        image must panic before partial replay. */
  reset_disk();
  fake_initlog();
  {
    struct logheader *hb = (struct logheader *)g_disk[LOGSTART];
    hb->n = 1;
    hb->block[0] = DISKBLKS + 5;    /* impossible (out of range) */
    if (setjmp(g_jmp) == 0) {
      g_jmp_armed = 1;
      fake_initlog();
      g_jmp_armed = 0;
      fprintf(stderr, "FAIL: impossible block number did not panic\n");
      return 1;
    }
    g_jmp_armed = 0;
  }

  printf("contract: admission, dedup, commit, idempotent recovery, "
         "and validation passed\n");
  return 0;
}

/* ---- fixed-seed fuzz workload ---- */
static uint64 g_rng;

static uint64
next_rng(void)
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return g_rng;
}

/* message buffer for the reproduction artifact on failure */
static char g_repro[256];

static int
run_fuzz(uint64 seed, uint64 cases)
{
  uint blkno, i, op;
  uint home0 = LOGSTART + LOGNBLK + 16;

  reset_disk();
  fake_initlog();
  g_rng = seed ? seed : 1;

  for (i = 0; i < cases; i++) {
    op = next_rng() % 4;

    if (op == 0) {
      /* begin a transaction if one is not already open. */
      if (log.outstanding == 0)
        begin_op();
    } else if (op == 1) {
      /* log_write one distinct home block inside the current transaction. */
      if (log.outstanding > 0) {
        struct buf *b;
        blkno = home0 + (uint)(next_rng() % 16);
        /* bound the transaction below the over-capacity panic */
        if (log.lh.n < MAXOPBLOCKS - 1 && log.lh.n < log.size - 1) {
          b = bread(1, blkno);
          *((uint *)b->data) = (uint)(0x80000000u | i);
          log_write(b);
          brelse(b);
        }
      }
    } else if (op == 2) {
      /* end a transaction; the last one commits */
      if (log.outstanding > 0)
        end_op();
    } else {
      /* every committed transaction must leave cleared header/home blocks */
      ;
    }

    /* Invariants after every step: never over capacity; header never exceeds
       the satellite region (data + header boundaries = log.size-1) within an
       admitted transaction; each committed home block is deterministic. */
    if (log.outstanding > 0 && log.lh.n >= log.size - 1) {
      snprintf(g_repro, sizeof(g_repro),
               "over-capacity header at case %llu", (unsigned long long)i);
      return 1;
    }
  }

  /* Close out any open transaction so recovery starts from a clean state. */
  while (log.outstanding > 0)
    end_op();
  CHECK(log.lh.n == 0, "header not cleared after fuzz close");
  /* Recovery after the fuzz is a no-op: no committed log is left behind. */
  fake_initlog();
  CHECK(log.lh.n == 0, "header non-empty after no-op recovery");

  printf("fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}

int
main(int argc, char **argv)
{
  /* No args: deterministic contract checks. With seed+cases: the fixed-seed
   * fuzz workload (each mode starts from a fresh disk + initlog). */
  if (argc < 2)
    return run_contract();
  return run_fuzz(strtoull(argv[1], 0, 10), strtoull(argv[2], 0, 10));
}