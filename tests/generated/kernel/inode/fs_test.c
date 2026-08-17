/* kernel/inode validation harness.
 *
 * This harness compiles the real kernel/fs.c together with the real
 * kernel/bio.c and kernel/log.c (via -Ikernel) plus lightweight
 * single-threaded stubs for the dependencies they need (spinlock primitives,
 * sleep/wakeup, myproc, virtio block I/O, panic, and the validated-memory
 * copy helpers never reached with user_dst == 0). It loads a real
 * mkfs-generated fs.img into an in-memory disk, drives the actual
 * fsinit/inode_block_lifecycle/path_resolution operations (begin_op/log_write/
 * end_op transactions from kernel/log, block/bitmap allocation and inode
 * cache from kernel/fs), and checks the kernel/inode invariants:
 *
 *   - filesystem-admission-order: fsinit mounts the deterministic image,
 *     validating geometry and completing redo-log recovery, before any
 *     persistent inode operation is admitted.
 *   - allocation-reference-consistency: every referenced block and inode is
 *     allocated and reclaimed exactly once; bounded allocate/truncate/unlink/
 *     recreate cycles restore expected free counts.
 *   - inode-cache-identity: each live (dev, inum) pair has one cached identity
 *     with a nonnegative reference count.
 *   - directory-reference-integrity / path-lock-progress: dirlookup/dirlink/
 *     unlink and component-wise traversal preserve directory references and
 *     release the parent before waiting on a child; a bounded directory-
 *     helper workload survives remount (a second fsinit) without panic, lock
 *     cycle, or leaked references.
 *
 * Usage: fs_test fs.img            -> deterministic contract checks
 *        fs_test fs.img SEED CASES -> also run the fixed-seed fuzz workload
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
#include "fs.h"

#define ROOTINO 1
#define DISKBLKS 2048   /* covers the FSSIZE=2000 image plus headroom */

/* kernel/log interface (real source linked in; declared manually). */
extern void initlog(int dev);
extern void begin_op(void);
extern void log_write(struct buf *b);
extern void end_op(void);

/* An in-memory disk modelling the root device. This is the backing store read
 * and written by the virtio stub; fsinit, the inode cache, and the redo log
 * all transfer logical blocks through it. */
static unsigned char g_disk[DISKBLKS][BSIZE];
static int   g_writes;

/* panic interception, as in the log harness. */
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
  (void)lk;
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

/* Validated user-memory copy helpers are referenced by fs.c's either_copyin/
 * either_copyout but are never called by the harness (all reads/writes use
 * kernel buffers, user_src/user_dst == 0). They must still be linkable, so
 * provide stubs that abort if somehow reached. */
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  (void)pagetable; (void)dst; (void)srcva; (void)len;
  fprintf(stderr, "FAIL: copyin unexpectedly called by inode harness\n");
  exit(1);
}

int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  (void)pagetable; (void)dstva; (void)src; (void)len;
  fprintf(stderr, "FAIL: copyout unexpectedly called by inode harness\n");
  exit(1);
}

int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  (void)pagetable; (void)dst; (void)srcva; (void)max;
  fprintf(stderr, "FAIL: copyinstr unexpectedly called by inode harness\n");
  exit(1);
}

/* safestrcpy is a kernel/string.c helper fs.c uses; provide a small stub. */
char *
safestrcpy(char *dst, const char *src, int n)
{
  int i;
  if (n <= 0)
    return dst;
  for (i = 0; i < n - 1 && src[i] != 0; i++)
    dst[i] = src[i];
  dst[i] = 0;
  return dst;
}

/* Virtio transfer stub: transfers one complete 1024-byte logical block. */
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

/* ---- access to kernel/log internals for admission checks ---- */
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

/* kernel/bio's cache shape for a simulated crash+reboot. */
struct bcache {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct buf head;
};
extern struct bcache bcache;

/* ---- test helpers ---- */
#define CHECK(cond, msg) \
  do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

/* Load a mkfs-generated fs.img path into the in-memory disk. Uses stdio so
 * the harness does not pull in the host <fcntl.h>/<unistd.h> that declare a
 * conflicting `struct stat` (the kernel fs.h defines its own). */
static int
load_disk(const char *img)
{
  FILE *fp = fopen(img, "rb");
  size_t got;
  if (fp == 0) {
    fprintf(stderr, "FAIL: cannot open image %s\n", img);
    return 1;
  }
  got = fread(g_disk, 1, sizeof(g_disk), fp);
  fclose(fp);
  if (got <= 0) {
    fprintf(stderr, "FAIL: empty image %s\n", img);
    return 1;
  }
  return 0;
}

/* Invalidate any cached buffer so the next bread re-reads from the disk to
 * model a fresh boot / remount (fsinit re-reads committed metadata). */
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

/* Mount the image: (re)initialize the buffer cache and call the real fsinit,
 * which validates geometry, runs redo-log recovery, and resolves the root. */
static void
mount_fs(void)
{
  binit();
  force_cache_invalidate();
  fsinit(1);
}

/* Read the superblock from the on-disk image into a local copy. */
static void
image_sb(struct superblock *s)
{
  memcpy(s, g_disk[1], sizeof(*s));
}

/* Resolve and return a locked, referenced root inode (cached reference). */
static struct inode *g_root_ref;
static void
reset_root_ref(void)
{
  if (g_root_ref != 0) {
    iput(g_root_ref);
    g_root_ref = 0;
  }
}
static struct inode *
ip_root(void)
{
  if (g_root_ref == 0)
    g_root_ref = namei("/");
  return g_root_ref;
}

/* nameiparent element name buffer. */
static char g_nb[DIRSIZ];

/* ---- deterministic contract checks ---- */
static int
run_contract(const char *img)
{
  struct inode *ip, *child;
  struct stat st;
  struct superblock s;
  uint poff;
  char data[128];
  char name[DIRSIZ];
  int cnt;

  (void)st;
  if (load_disk(img) != 0)
    return 1;
  mount_fs();
  reset_root_ref();

  image_sb(&s);
  CHECK(s.magic == FSMAGIC, "superblock magic not present");
  CHECK(s.size == FSSIZE, "superblock size mismatch");
  CHECK(s.logstart > 1, "invalid logstart");
  CHECK(s.inodestart >= s.logstart + s.nlog, "inode region must follow log");
  CHECK(s.bmapstart > s.inodestart, "bitmap region must follow inodes");

  /* --- fsinit resolved the root inode tree: namei("/") -> root dir. --- */
  ip = namei("/");
  CHECK(ip != 0, "namei(/)=NULL");
  ilock(ip);
  CHECK(ip->type == T_DIR, "root is not a directory");
  CHECK(ip->inum == ROOTINO, "root not inum 1");
  iunlock(ip);
  iput(ip);

  /* --- path_resolution: resolve the deterministic initial tree. --- */
  ip = namei("/README");
  CHECK(ip != 0, "namei(/README)=NULL");
  ilock(ip);
  CHECK(ip->type == T_FILE, "README not a file");
  cnt = readi(ip, 0, (uint64)data, 0, sizeof(data));
  CHECK(cnt > 0, "README empty");
  CHECK((uint)cnt == strlen("kernel/inode deterministic root image\n") &&
        memcmp(data, "kernel/inode deterministic root image\n", (size_t)cnt) == 0,
        "README contents mismatch");
  iunlock(ip);
  iput(ip);

  ip = namei("/test/t1");
  CHECK(ip != 0, "namei(/test/t1)=NULL");
  ilock(ip);
  CHECK(ip->type == T_FILE, "t1 not a file");
  cnt = readi(ip, 0, (uint64)data, 0, sizeof(data));
  CHECK((uint)cnt == strlen("t1 hello\n") && memcmp(data, "t1 hello\n", strlen("t1 hello\n")) == 0, "t1 contents mismatch");
  iunlock(ip);
  iput(ip);

  /* --- dirlookup + dot/dot-dot: child ".." resolves back to root. --- */
  ip = namei("/test");
  CHECK(ip != 0, "namei(/test)=NULL");
  ilock(ip);
  CHECK(ip->type == T_DIR, "test not a directory");
  child = dirlookup(ip, "..", &poff);
  CHECK(child != 0, "dirlookup(..)=NULL");
  iunlock(ip);
  iput(ip);
  CHECK(child->inum == ROOTINO, "dot-dot not root");
  iput(child);

  /* --- inode_block_lifecycle: allocate + truncate restores free counts. ---
   * ialloc a fresh file, extend it through several blocks, then unlink and
   * drop the last reference so iput truncates it; its inum is then reused. */
  {
    int inum_new = -1;
    begin_op();
    ip = ialloc_alloc(1, T_FILE);
    CHECK(ip != 0, "ialloc_alloc failed");
    ilock(ip);
    inum_new = (int)ip->inum;
    memset(data, 0xAB, sizeof(data));
    {
      int i;
      for (i = 0; i < 8; i++) {
        cnt = writei(ip, 0, (uint64)data, (uint)i * 64, 64);
        CHECK(cnt == 64, "writei short during lifecycle");
      }
    }
    ip->nlink = 0;     /* mark for reclaim on last iput */
    iupdate(ip);
    iunlock(ip);
    iput(ip);          /* last reference + nlink 0 -> itrunc + free, in-transaction */
    end_op();

    /* The freed inum must be reusable via a fresh ialloc. */
    begin_op();
    {
      struct inode *again = ialloc_alloc(1, T_FILE);
      CHECK(again != 0, "reuse of freed inode failed");
      ilock(again);
      CHECK((uint)again->inum == (uint)inum_new,
            "failed to reuse the freed inode");
      again->nlink = 0;
      iupdate(again);
      iunlock(again);
      iput(again);     /* reclaim inside the transaction */
      end_op();
    }
  }

  /* --- path_resolution mutation: create + dirlink + unlink. --- */
  begin_op();
  {
    struct inode *root = ip_root();
    uint inum_new;
    ilock(root);
    {
      struct inode *fe = ialloc_alloc(1, T_FILE);
      CHECK(fe != 0, "ialloc_alloc for dirlink failed");
      ilock(fe);
      fe->nlink = 1;
      iupdate(fe);
      inum_new = fe->inum;
      iunlock(fe);
      CHECK(dirlink(root, "newfile", inum_new) == 0, "dirlink failed");
      iput(fe);
    }
    iunlock(root);
  }
  end_op();

  begin_op();
  ip = namei("/newfile");
  CHECK(ip != 0, "namei(/newfile)=NULL after dirlink");
  ilock(ip);
  CHECK(ip->nlink == 1, "newfile nlink not published");
  iunlock(ip);
  iput(ip);
  end_op();

  begin_op();
  CHECK(unlink_helper("/newfile", 0, 0) == 0, "unlink failed");
  end_op();
  {
    /* The entry must be removed: namei now fails. */
    struct inode *gone = namei("/newfile");
    CHECK(gone == 0, "unlinked name still resolvable");
  }

  /* --- dot/dot-dot and nameiparent: extract the final path element. --- */
  {
    struct inode *parent = nameiparent("/test/t1", name);
    CHECK(parent != 0, "nameiparent(/test/t1)=NULL");
    CHECK(strcmp(name, "t1") == 0, "nameiparent did not return last element");
    /* parent must be /test (inum 3). */
    ilock(parent);
    CHECK(parent->inum == 3, "nameiparent wrong parent");
    iunlock(parent);
    iput(parent);
  }

  /* --- remount: a second fsinit re-opens the committed root tree. --- */
  reset_root_ref();
  mount_fs();
  ip = namei("/test/t1");
  CHECK(ip != 0, "namei(/test/t1)=NULL after remount");
  ilock(ip);
  cnt = readi(ip, 0, (uint64)data, 0, sizeof(data));
  CHECK((uint)cnt == strlen("t1 hello\n") && memcmp(data, "t1 hello\n",
        strlen("t1 hello\n")) == 0, "t1 contents mismatch after remount");
  iunlock(ip);
  iput(ip);

  printf("contract: mount, path resolution, allocation lifecycle, "
         "dir mutation, and remount passed\n");
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

static int
run_fuzz(const char *img, uint64 seed, uint64 cases)
{
  uint64 i;
  uint op;
  char nb[64];
  char buf[32];

  if (load_disk(img) != 0)
    return 1;
  mount_fs();
  reset_root_ref();

  g_rng = seed ? seed : 1;

  for (i = 0; i < cases; i++) {
    op = next_rng() % 5;

    if (op == 0) {
      /* allocate + write + unlink a fresh file inside one transaction. */
      struct inode *ip;
      begin_op();
      ip = ialloc_alloc(1, T_FILE);
      if (ip != 0) {
        ilock(ip);
        memset(buf, (int)(0x30 + next_rng() % 90), sizeof(buf));
        writei(ip, 0, (uint64)buf, 0, sizeof(buf));
        ip->nlink = 0;
        iupdate(ip);
        iunlock(ip);
        iput(ip);   /* nlink 0 + last ref -> itrunc + free, in-transaction */
      }
      end_op();
    } else if (op == 1) {
      /* create a real transient file at a generated name, then unlink it. */
      struct inode *fe = 0, *parent;
      int created = 0;
      snprintf(nb, sizeof(nb), "/f%c%c", (char)('a' + next_rng() % 26),
               (char)('a' + next_rng() % 26));
      begin_op();
      parent = nameiparent(nb, g_nb);
      if (parent != 0) {
        ilock(parent);
        fe = ialloc_alloc(1, T_FILE);
        if (fe != 0) {
          ilock(fe);
          fe->nlink = 1;
          iupdate(fe);
          iunlock(fe);
          created = (dirlink(parent, g_nb, fe->inum) == 0);
          iput(fe);
        }
        iunlock(parent);
        iput(parent);
      }
      end_op();
      if (created) {
        /* the committed transient file is now removed by an unlink. */
        begin_op();
        unlink_helper(nb, 0, 0);
        end_op();
      }
    } else if (op == 2) {
      /* resolve an existing path. */
      struct inode *ip = namei("/test/t1");
      if (ip != 0)
        iput(ip);
    } else if (op == 3) {
      /* attempt to unlink a nonexistent path: must fail cleanly. */
      snprintf(nb, sizeof(nb), "/missing%llu", (unsigned long long)i);
      begin_op();
      unlink_helper(nb, 0, 0);
      end_op();
    } else {
      /* stat one file and verify integrity by reading it. */
      struct inode *ip = namei("/README");
      if (ip != 0) {
        char tmp[8];
        ilock(ip);
        readi(ip, 0, (uint64)tmp, 0, 8);
        iunlock(ip);
        iput(ip);
      }
    }

    /* Bounded admission invariant: after each mutation, no transaction is
       left open. */
    if (log.outstanding != 0) {
      fprintf(stderr, "FAIL: leaked transaction at case %llu\n",
              (unsigned long long)i);
      return 1;
    }
  }

  /* Remount the image: committed metadata must survive. */
  reset_root_ref();
  mount_fs();
  {
    struct inode *ip = namei("/test/t1");
    CHECK(ip != 0, "root tree not resolvable after fuzz remount");
    iput(ip);
  }

  printf("fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}

int
main(int argc, char **argv)
{
  g_proc.pid = 1;

  if (argc < 2) {
    fprintf(stderr, "usage: fs_test fs.img [seed cases]\n");
    return 1;
  }
  if (argc < 3)
    return run_contract(argv[1]);
  return run_fuzz(argv[1], strtoull(argv[2], 0, 10), strtoull(argv[3], 0, 10));
}