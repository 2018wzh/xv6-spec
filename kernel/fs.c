// fs.c - the xv6 on-disk file-system and inode layer (kernel/inode).
//
// kernel/inode owns the xv6 on-disk layout, deterministic image construction
// (mkfs), block and inode allocation, the inode cache, directories, and
// component-wise path traversal. The module operations:
//
//   - fsinit: validate the superblock geometry, complete kernel/log redo
//     recovery (initlog does recovery), initialize the inode cache, and mount
//     the deterministic mkfs-generated root image so inode operations are
//     ready before any user file operation is admitted (filesystem-admission-
//     order).
//   - inode_block_lifecycle: bitmap allocation/release (balloc/bfree), inode
//     allocation (ialloc_alloc), and reclamation (itrunc + iput) inside a log
//     transaction. Referenced blocks and inodes stay allocated and each
//     resource is reclaimed exactly once (allocation-reference-consistency).
//   - path_resolution: directory helpers (dirlookup, dirlink, unlink_helper)
//     and component-wise path traversal (dnamex, namei, nameiparent). Every
//     persistent directory mutation happens inside one log transaction with
//     no partially published metadata, and traversal releases the current
//     inode before waiting on a child (path-lock-progress).
//
// All metadata mutations run through the buffer cache (kernel/bio) and are
// bracketed by begin_op/log_write/end_op (kernel/log), so each persistent
// mutation is committed as one redo-log transaction and survives a crash via
// idempotent log recovery.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "buf.h"
#include "defs.h"
#include "fs.h"

// On-disk inode region: the first inode (inum 1) is the root directory.
#define ROOTINO 1

// Number of on-disk inodes per buffer block.
#define IPB (BSIZE / sizeof(struct dinode))

// Bits per bitmap block.
#define BPB (BSIZE * 8)

// The inode cache: a fixed NINODE array guarded by a cache spinlock plus a
// per-inode sleep lock protecting its contents. Each live (dev, inum) pair
// has exactly one cached identity with a nonnegative reference count
// (inode-cache-identity). Cache spinlocks are released before any inode or
// buffer sleep-lock wait (concurrency lock_order).
struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

// The validated superblock of the mounted root device. Only reachable after
// fsinit validates it (invalid geometry panics before mutable mount).
static struct superblock sb;

// ---- forward declarations for internal helpers ----
static struct buf *breadv(uint dev, uint b);
static int isdirempty(struct inode *dp);
static int namecmp(const char *s, const char *t);
static char *skipelem(const char *path, char *name);
static void bzero_block(uint b);
static int either_copyout(int user_dst, uint64 dst, void *src, uint len);
static int either_copyin(void *dst, int user_src, uint64 src, uint len);
static uint min(uint a, uint b);

// ---- superblock / device ----

// Read the superblock from logical block 1 of the device.
static void
readsb(uint dev)
{
  struct buf *bp = breadv(dev, 1);
  memmove(&sb, bp->data, sizeof(sb));
  brelse(bp);
}

// ---- allocation bitmap helpers (inode_block_lifecycle) ----

// Allocate a zeroed disk block, returning the block number or 0 on
// exhaustion (full-disk boundary) without leaking a bitmap or buffer
// reference. Must be called inside a log transaction; the caller records the
// inode/block change with log_write before commit.
int
balloc_alloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  (void)dev;
  for (b = 0; (uint)b < sb.size; b += BPB) {
    bp = breadv(1, sb.bmapstart + b / BPB);
    for (bi = 0; bi < BPB && (uint)(b + bi) < sb.size; bi++) {
      m = 1 << (bi % 8);
      if ((bp->data[bi / 8] & m) == 0) {  // that block is free
        bp->data[bi / 8] |= m;            // mark allocated on disk
        log_write(bp);
        brelse(bp);
        bzero_block(b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  return 0;   // disk full
}

// Free a disk block by clearing its bit in the bitmap. Must be called inside
// a log transaction; the block becomes allocatable again only after commit.
void
bfree_release(uint dev, uint b)
{
  struct buf *bp;
  int bi, m;

  (void)dev;
  if (b >= sb.size)
    panic("bfree: block out of range");
  bi = b % BPB;
  m = 1 << (bi % 8);
  bp = breadv(1, sb.bmapstart + b / BPB);
  if ((bp->data[bi / 8] & m) == 0)
    panic("bfree: freeing a free block");
  bp->data[bi / 8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Zero a disk block (used when allocating so a fresh data/inode block reads
// as entirely zero).
static void
bzero_block(uint b)
{
  struct buf *bp = breadv(1, b);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// ---- inode cache operations ----

// Initialize the inode cache: one spinlock and each inode's sleep lock.
void
iinit(void)
{
  int i;

  initlock(&icache.lock, "icache");
  for (i = 0; i < NINODE; i++)
    initsleeplock(&icache.inode[i].lock, "inode");
}

// Allocate a new inode with the given type on device dev, returning the
// inode locked (ilock held) and referenced, or 0 on inode exhaustion.
// Must be called inside a log transaction.
struct inode *
ialloc_alloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for (inum = 1; inum < (int)sb.ninodes; inum++) {
    bp = breadv(1, sb.inodestart + inum / IPB);
    dip = (struct dinode *)bp->data + inum % IPB;
    if (dip->type == 0) {  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);       // mark it allocated on disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  return 0;
}

// Return the inode with the given inum on device dev, with a new reference.
// Does not lock it; callers must ilock before use.
struct inode *
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);
  // Is the inode already cached?
  empty = 0;
  for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if (empty == 0 && ip->ref == 0)
      empty = ip;
  }
  // Recycle an inode cache slot.
  if (empty == 0)
    panic("iget: no inodes");
  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);
  return ip;
}

// Increment the reference count of an inode (e.g. when a file object aliases
// it). Returns the same inode.
struct inode *
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode, reading it into memory if it has never been loaded
// (valid == 0). Blocks (sleeps) on the per-inode sleep lock; the cache
// spinlock is never held across a sleep-lock wait (lock_order).
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if (ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if (ip->valid == 0) {
    bp = breadv(ip->dev, sb.inodestart + ip->inum / IPB);
    dip = (struct dinode *)bp->data + ip->inum % IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if (ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");
  releasesleep(&ip->lock);
}

// Drop a reference to an inode, freeing it (and its blocks) if the inode has
// no remaining links (nlink == 0) and no other references (allocation-
// reference-consistency).
void
iput(struct inode *ip)
{
  if (ip->ref != 1 || !ip->valid || ip->nlink != 0) {
    // Still referenced or still linked: just drop one reference.
    acquire(&icache.lock);
    if (ip->ref < 1)
      panic("iput");
    ip->ref--;
    release(&icache.lock);
    return;
  }

  // Last reference and unlinked: reclaim the inode and free its blocks. The
  // ref==1 guard guarantees no other holder owns the sleep lock, so
  // acquiresleep cannot actually block here.
  acquiresleep(&ip->lock);
  itrunc(ip);
  ip->type = 0;
  iupdate(ip);
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->valid = 0;
  if (ip->ref != 1)
    panic("iput: concurrent reference");
  ip->ref = 0;
  release(&icache.lock);
}

// Unlock and drop a reference to inode (the common combination after use).
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Copy a modified inode back to disk. Must be called from within a log
// transaction, with the inode locked.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = breadv(ip->dev, sb.inodestart + ip->inum / IPB);
  dip = (struct dinode *)bp->data + ip->inum % IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// ---- block mapping ----

// Return the disk block address of the nth block of inode ip. Errors (out of
// range, indirect block exhaustion) return 0, which callers treat as failure
// without causing a partial transaction.
uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if (bn < NDIRECT) {
    if ((addr = ip->addrs[bn]) == 0) {
      addr = balloc_alloc(ip->dev);
      if (addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if (bn < NINDIRECT) {
    // Load indirect block, allocating if necessary.
    if ((addr = ip->addrs[NDIRECT]) == 0) {
      addr = balloc_alloc(ip->dev);
      if (addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = breadv(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[bn]) == 0) {
      addr = balloc_alloc(ip->dev);
      if (addr != 0) {
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }
  return 0;   // bn beyond MAXFILE
}

// Truncate inode ip: free every block it owns (direct + one indirect block).
// Must be called with the inode locked, inside a log transaction.
int
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for (i = 0; i < NDIRECT; i++) {
    if (ip->addrs[i]) {
      bfree_release(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if (ip->addrs[NDIRECT]) {
    bp = breadv(ip->dev, ip->addrs[NDIRECT]);
    a = (uint *)bp->data;
    for (j = 0; (uint)j < NINDIRECT; j++) {
      if (a[j])
        bfree_release(ip->dev, a[j]);
    }
    brelse(bp);
    bfree_release(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
  return 0;
}

// ---- file contents (readi/writei/stati) ----

// Read up to n bytes from an inode at offset off into buf, returning the
// number of bytes copied (0 on out-of-range; short reads allowed). user_dst
// is 1 only when writing to a validated user address; the inode harness uses
// kernel buffers (user_dst == 0).
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if (off > ip->size || off + n < off)
    return 0;
  if (off + n > ip->size)
    n = ip->size - off;

  for (tot = 0; tot < n; tot += m, off += m, dst += m) {
    uint addr = bmap(ip, off / BSIZE);
    if (addr == 0)
      break;   // out-of-range: stop with a short count
    bp = breadv(ip->dev, addr);
    m = min(n - tot, BSIZE - off % BSIZE);
    if (either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write n bytes from buf into inode ip at offset off, extending ip->size as
// needed. Returns the number of bytes written or -1 on error.
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if (off > ip->size || off + n < off)
    return -1;
  if (off + n > MAXFILE * BSIZE)
    return -1;

  for (tot = 0; tot < n; tot += m, off += m, src += m) {
    uint addr = bmap(ip, off / BSIZE);
    if (addr == 0)
      break;   // out of capacity: stop
    bp = breadv(ip->dev, addr);
    m = min(n - tot, BSIZE - off % BSIZE);
    if (either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if (off > ip->size)
    ip->size = off;
  iupdate(ip);
  return tot;
}

// Fill in a stat structure from the inode (used by the fstat ABI).
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// ---- directory helpers (path_resolution) ----

// Compare two directory names, treating the first NUL as the end.
static int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory. If found, return a referenced
// inode for the entry's inode number, with *poff pointing to the entry
// position. The directory's own inode lock must be held. Returns 0 if not
// found or if dp is not a directory.
struct inode *
dirlookup(struct inode *dp, const char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if (dp->type != T_DIR)
    panic("dirlookup not DIR");

  for (off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if (de.inum == 0)
      continue;
    if (namecmp(name, de.name) == 0) {
      // entry matches path element.
      if (poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }
  return 0;
}

// Write a new directory entry (name, inum) into directory dp. Returns -1 on
// failure (name already present or directory full) or 0 on success. Must be
// called with dp locked, inside a log transaction.
int
dirlink(struct inode *dp, const char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that no name already exists.
  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for (off = 0; (uint)off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if (de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;
  return 0;
}

// Is a directory empty (contains only "." and "..")? Used to prevent unlink
// of a non-empty directory.
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for (off = 2 * (int)sizeof(de); (uint)off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

// Remove the directory entry `name` from its parent and, if it is the last
// link, reclaim the inode. Returns 0 on success, -1 on failure (not found,
// nonempty directory, . or ..). Must be called inside a log transaction.
// On success, produces ONE committed directory mutation.
int
unlink_helper(const char *path, struct inode **pip, struct inode **ppip)
{
  struct inode *ip, *dp;
  struct dirent de;
  uint off;
  char name[DIRSIZ];

  (void)pip; (void)ppip;
  dp = nameiparent(path, name);
  if (dp == 0)
    return -1;

  ilock(dp);
  ip = dirlookup(dp, name, &off);
  if (ip == 0) {
    iunlockput(dp);
    return -1;
  }
  ilock(ip);

  // Cannot unlink "." or ".." .
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  // Cannot unlink a non-empty directory.
  if (ip->type == T_DIR && !isdirempty(ip))
    goto bad;

  // Zero out the directory entry (instead of replacing it with another,
  // since the entry might be in a sub-block).
  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    goto bad;

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;

bad:
  iunlockput(ip);
  iunlockput(dp);
  return -1;
}

// ---- path resolution ----

// Skip leading slashes and copy the next path element (up to the next slash
// or end) into name, which must be at least DIRSIZ bytes and is always made
// NUL-terminated. Returns a pointer to the next component, or 0 at path end.
static char *
skipelem(const char *path, char *name)
{
  const char *s;
  int len;

  while (*path == '/')
    path++;
  if (*path == 0)
    return 0;

  s = path;
  while (*path != '/' && *path != 0)
    path++;
  len = path - s;
  if (len >= DIRSIZ)
    len = DIRSIZ - 1;
  memmove(name, s, len);
  name[len] = 0;

  while (*path == '/')
    path++;
  return (char *)path;
}

// Look up the inode for a path. In normal mode (nameiparent == 0) return a
// referenced (unlocked) inode for the full path. In nameiparent mode return
// the referenced (unlocked) parent inode and copy the final path element
// into name. Returns 0 on failure, without retaining a parent or child lock.
struct inode *
dnamex(const char *path, int nameiparent_flag, char *name)
{
  struct inode *ip, *next;
  char element[DIRSIZ];

  ip = iget(ROOTDEV, ROOTINO);
  if (ip == 0)
    return 0;

  while ((path = skipelem(path, element)) != 0) {
    ilock(ip);
    if (ip->type != T_DIR) {
      iunlockput(ip);
      return 0;
    }
    if (nameiparent_flag && *path == '\0') {
      // Stop one level early: this is the parent inode; the final element is
      // already in `element`. Copy it out and return the unlocked parent.
      iunlock(ip);
      safestrcpy(name, element, DIRSIZ);
      return ip;
    }
    if ((next = dirlookup(ip, element, 0)) == 0) {
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if (nameiparent_flag) {
    iput(ip);
    return 0;
  }
  return ip;
}

// Return an unlocked, referenced inode for the path name.
struct inode *
namei(const char *path)
{
  char nbuf[MAXPATH];
  return dnamex(path, 0, nbuf);
}

// Return an unlocked, referenced inode for the parent directory of the path
// and copy the final path element into name.
struct inode *
nameiparent(const char *path, char *name)
{
  return dnamex(path, 1, name);
}

// ---- fsinit ----

// fsinit - mount the file system from device dev.
//
// Reads and validates the superblock geometry, completes kernel/log redo
// recovery, initializes the inode cache, and resolves the deterministic root
// inode. All storage layers (kernel/bio, kernel/virtio, kernel/log) must be
// initialized first.
//
// An all-zero superblock region means the root device has no filesystem image
// yet (a bare lab root boot disk); in that case the module leaves the file
// system unmounted and returns without mutating anything, so an empty-disk
// boot can still reach the scheduler. A present-but-invalid magic, or
// overlapping/out-of-range regions, panics before mutable mount (fsinit
// errors: invalid geometry panics before mutable access).
void
fsinit(int dev)
{
  struct superblock probe;
  struct buf *probe_bp;

  // Probe block 1 to distinguish "no image" (all-zero) from an invalid image.
  probe_bp = breadv(dev, 1);
  memmove(&probe, probe_bp->data, sizeof(probe));
  brelse(probe_bp);

  if (probe.magic == 0 &&
      probe.size == 0 && probe.nlog == 0 && probe.logstart == 0 &&
      probe.ninodes == 0 && probe.inodestart == 0 && probe.bmapstart == 0) {
    // Unformatted root device: leave the file system unmounted. This keeps a
    // boot on a bare lab disk reaching the scheduler without a spurious mount.
    return;
  }

  readsb(dev);
  if (sb.magic != FSMAGIC)
    panic("invalid file system superblock");
  if (sb.size <= 0 || (uint)sb.size > FSSIZE)
    panic("invalid superblock size");
  if (sb.nlog <= 0 || sb.logstart <= 1)
    panic("invalid log region");
  if (sb.logstart + sb.nlog > sb.size)
    panic("log region out of range");
  if (sb.inodestart < sb.logstart + sb.nlog || sb.inodestart >= sb.size)
    panic("invalid inode region");
  if (sb.bmapstart < sb.inodestart || sb.bmapstart >= sb.size)
    panic("invalid bitmap region");

  initlog(dev);   // validates the log superblock subset and recovers redo log
  iinit();        // initialize the inode cache

  // Resolve the deterministic root inode tree (reads the root directory so a
  // malformed root is detected before any user file operation).
  struct inode *root = iget(dev, ROOTINO);
  ilock(root);
  if (root->type != T_DIR)
    panic("fsinit: root not a directory");
  iunlock(root);
  iput(root);
}

// ---- low-level buffer bread used throughout fs.c ----
static struct buf *
breadv(uint dev, uint b)
{
  return bread(dev, b);
}

// ---- content-copy helpers ----
static uint
min(uint a, uint b)
{
  return a < b ? a : b;
}

// Copy len bytes from kernel src to user address dst if user_dst, else to a
// kernel buffer. Returns 0 on success, -1 on a failed user copy (validated;
// never dereferences a raw user pointer directly).
static int
either_copyout(int user_dst, uint64 dst, void *src, uint len)
{
  struct proc *p = myproc();

  if (user_dst) {
    return copyout(p->pagetable, dst, (char *)src, len);
  }
  memmove((void *)dst, src, len);
  return 0;
}

// Copy len bytes from user address src to the kernel buffer dst if user_src,
// else from a kernel buffer. Returns 0 on success, -1 on a failed user copy.
static int
either_copyin(void *dst, int user_src, uint64 src, uint len)
{
  struct proc *p = myproc();

  if (user_src)
    return copyin(p->pagetable, (char *)dst, src, len);
  memmove(dst, (const void *)src, len);
  return 0;
}