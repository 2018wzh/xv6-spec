// log.c - The bounded physical redo log (kernel/log).
//
// This module owns the bounded redo-log region on the root file system:
// admission control (begin_op/end_op), a distinct-home-block in-memory header
// (log_write), write-ahead data, a durable nonempty commit header, a
// checkpoint that installs home blocks and unpins each logged buffer once,
// and an idempotent recovery (initlog -> recover_from_log). It is the
// canonical xv6 group-commit redo log adapted to this module spec.
//
// Properties honored here:
//   - redo-ordering: log data reaches storage (write_log) before the nonempty
//     commit header (write_head), and home blocks change only after that
//     header (install_trans runs after write_head is durable).
//   - committed-header-boundary: a durable nonempty header is the sole commit
//     point separating discardable and replayable transactions.
//   - logged-block-unique: each home block appears at most once in the current
//     log header (log_write dedups by home block number).
//   - recovery-before-admission: initlog completes header validation and
//     recovery from the log before the first begin_op may be admitted.
//   - committed-header-boundary + recovery-idempotent: recovery validates
//     every header entry against the superblock before changing any home
//     block, and repeating it is a no-op.
//
// Concurrency: one spinlock guards transaction admission and in-memory header
// membership. The commit path sleeps/writes with the log lock released
// (interrupt_rules / forbidden_patterns: no buffer/disk I/O while holding the
// log spinlock). The caller of log_write holds the buffer sleep lock, then a
// bounded log spinlock section records the distinct home block (lock_order).

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "buf.h"

// ---- on-disk log region and superblock geometry ----
//
// The log relies on a validated superblock located at logical block 1
// supplying an in-range fixed log region (logstart together with nlog blocks).
// Only the subset of the file-system layout this module's recovery needs is
// declared here; the full superblock belongs to the later file-system slice.

#define FSMAGIC 0x10203040

// Superblock subset consulted by initlog/recover_from_log. layout: block 0
// unused, block 1 superblock, then nlog log blocks, then home blocks.
struct superblock {
  uint magic;       // must equal FSMAGIC
  uint size;        // total size of the file-system image (blocks)
  uint nblocks;     // number of data blocks
  uint ninodes;     // number of inodes
  uint nlog;        // number of log blocks in the log region
  uint logstart;    // block number of the first log block
  uint inodestart;  // block number of the first inode block
  uint bmapstart;   // block number of the first free map block
};

static struct superblock sb;

// The on-disk log header stored in the first block of the log region
// (log.start). It names how many log data blocks there are and, for each, the
// home block number it will be installed into at checkpoint/recovery.
struct logheader {
  int n;                // number of logged blocks in this transaction
  int block[LOGSIZE];   // home block number of each logged (distinct) block
};

// The in-memory log state. One spinlock guards admission (outstanding) and
// the in-memory header (lh) so that operation admission and distinct-block
// header insertion are atomic. `committing` records the sole commit owner.
struct log {
  struct spinlock lock;
  int start;          // block number of first log block
  int size;           // number of log blocks in the log region
  int outstanding;    // number of open (admitted) file-system operations
  int committing;     // a commit is in progress (at most one)
  int dev;            // device of the log region's home blocks
  struct logheader lh;
};
struct log log;

// Read the validated superblock from logical block 1 (standard xv6 layout).
static void
readsb(struct superblock *sb)
{
  struct buf *bp = bread(log.dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Write the in-memory header to the first log block (the durable commit point
// only when n > 0). bwrite completes before returning (kernel/bio rely).
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *)(buf->data);
  int i;

  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++)
    hb->block[i] = log.lh.block[i];
  bwrite(buf);
  brelse(buf);
}

// Read the on-disk header into memory, validating every entry against the
// superblock before the caller may change any home block. Corrupt headers,
// impossible block numbers, and oversized transactions panic before partial
// replay (errors). Validation is therefore an explicit pre-stage of recovery.
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *)(buf->data);
  int i;

  if (hb->n < 0 || hb->n >= log.size) {
    brelse(buf);
    panic("log: invalid header count");
  }
  for (i = 0; i < hb->n; i++) {
    // A logged block must be a home block: inside the image but outside the
    // log header/data region. Impossible numbers panic before replay.
    if ((uint)hb->block[i] < (uint)log.size + (uint)log.start ||
        (uint)hb->block[i] >= sb.size) {
      panic("log: impossible block number");
    }
  }

  log.lh.n = hb->n;
  for (i = 0; i < log.lh.n; i++)
    log.lh.block[i] = hb->block[i];
  brelse(buf);
}

// Copy the in-memory header's data blocks into their log slots (write-ahead
// of the log data). Runs with the log lock released.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start + tail + 1);
    struct buf *from = bread(log.dev, log.lh.block[tail]);
    memmove(to->data, from->data, BSIZE);
    bwrite(to);
    brelse(to);
    brelse(from);
  }
}

// Install the log's data blocks into their home blocks, unpinning each logged
// buffer exactly once when not recovering. Runs with the log lock released.
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start + tail + 1);
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]);
    memmove(dbuf->data, lbuf->data, BSIZE);
    bwrite(dbuf);
    if (recovering == 0)
      bunpin(dbuf);       // undo the pin placed on the home buffer by log_write
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Recover an interrupted transaction from the on-disk log. read_head validates
// every entry before install_trans changes any home block; after install the
// header is cleared so a second recovery is a no-op (recovery-idempotent).
static void
recover_from_log(void)
{
  read_head();
  install_trans(1);       // recovering: do not bunpin (never pinned)
  log.lh.n = 0;
  write_head();           // clear the header commit point
}

// group commit: write log data, write the durable commit header, install home
// blocks, then clear the header. Runs with the log lock released.
static void
commit(void)
{
  if (log.lh.n > 0) {
    write_log();
    write_head();
    install_trans(0);     // installs home blocks and unpins each logged buffer
    log.lh.n = 0;
    write_head();         // clear the commit header
  }
}

// initlog: initialize the log state and recover any interrupted transaction
// from the superblock-supplied log region before the first begin_op may be
// admitted (recovery-before-admission).
void
initlog(int dev)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("log: log header does not fit a block");

  initlock(&log.lock, "log");
  log.dev = dev;
  readsb(&sb);

  // Validate geometry before mount: magic, and an in-range, non-overlapping
  // log region (preconditions / fsinit-style invalid-geometry panic).
  if (sb.magic != FSMAGIC)
    panic("log: bad magic in superblock");
  if (sb.nlog <= 0 || sb.logstart <= 1)
    panic("log: invalid log region");
  if (sb.logstart + sb.nlog > sb.size)
    panic("log: log region out of range");

  log.start = sb.logstart;
  log.size = sb.nlog;
  log.lh.n = 0;
  log.outstanding = 0;
  log.committing = 0;

  recover_from_log();
}

// begin_op: admit one file-system operation, reserving worst-case capacity
// (MAXOPBLOCKS). Blocks (sleeps) while a commit is active or the reservation
// would exceed LOGSIZE; never exceeds LOGSIZE (log-admission-contract).
void
begin_op(void)
{
  acquire(&log.lock);
  while (1) {
    if (log.committing) {
      // Wait for the in-progress commit; wake_ after state is cleared.
      sleep(&log, &log.lock);
    } else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
      // This admission could exhaust the log; wait for the next commit.
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// log_write: record one distinct home block in the current transaction and pin
// its buffer until checkpoint calls bunpin exactly once. The caller must hold
// the buffer sleep lock and be inside an admitted operation (log_write pre).
void
log_write(struct buf *b)
{
  int i;

  acquire(&log.lock);
  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("log: too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of transaction");

  // logged-block-unique: each home block appears at most once in the header.
  for (i = 0; i < log.lh.n; i++) {
    if ((uint)log.lh.block[i] == b->blockno) {
      release(&log.lock);
      return;   // already logged: no new slot, no double-pin.
    }
  }

  log.lh.block[log.lh.n] = b->blockno;
  log.lh.n++;
  bpin(b);                        // pin persists until checkpoint bunpin.
  release(&log.lock);
}

// end_op: release one operation's admission; the last one performs the group
// commit. Runs the commit (and any disk waits) with the log lock released
// (interrupt_rules / forbidden_patterns).
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if (log.committing)
    panic("log committing during end_op");
  if (log.outstanding == 0) {
    // The last outstanding operation becomes the sole commit owner.
    do_commit = 1;
    log.committing = 1;
  } else {
    // Any capacity/admission waiters may retry now that a reservation released.
    wakeup(&log);
  }
  release(&log.lock);

  if (do_commit) {
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}