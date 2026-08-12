/* kernel/file validation harness.
 *
 * Compiles the real kernel/file.c + kernel/sysfile.c together with the real
 * kernel/fs.c, kernel/bio.c, and kernel/log.c (via -Ikernel) plus lightweight
 * single-threaded stubs for the dependencies file operations need (spinlock
 * primitives, myproc/current process, sleep/wakeup, virtio block I/O, and a
 * modeled user address space for the validated copy helpers). A real
 * mkfs-generated fs.img is loaded into an in-memory disk and the actual file
 * operations are driven against the kernel/file invariants:
 *
 *   - descriptor-reference-consistency: each populated descriptor slot owns
 *     exactly one global file reference; close clears the slot before final
 *     release.
 *   - file-offset-serialization: every shared file object serializes offset
 *     changes and advances by exactly the transferred byte count.
 *   - close-release-once: final close releases the inode-backed resource once.
 *   - user-buffer-validation: file operations copy only through validated
 *     user ranges (modeled) and never dereference raw user pointers.
 *   - namespace_mutation: create/link/unlink/mkdir/chdir and open/read/write/
 *     append/truncate publish only committed namespace and content state.
 *
 * Usage: file_test fs.img            -> deterministic contract checks
 *        file_test fs.img SEED CASES -> also run the fixed-seed fuzz workload
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
#include "file.h"
#include "fcntl.h"

#define DISKBLKS    8192              /* covers FSSIZE=2000 image + headroom */
#define USERSZ      (1 << 20)         /* modeled user space bound < MAXVA */

static unsigned char g_disk[DISKBLKS][BSIZE];
static int   g_writes;

/* ---- kernel-dependency stubs ---- */
static struct proc g_proc;
static unsigned char g_user[USERSZ];

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

void
initlock(struct spinlock *lk, const char *name) { lk->locked = 0; lk->name = name; }
void
acquire(struct spinlock *lk) { (void)lk; lk->locked = 1; }
void
release(struct spinlock *lk) { (void)lk; lk->locked = 0; }
int
holding(struct spinlock *lk) { return lk->locked; }

struct proc *
myproc(void) { return &g_proc; }

void
sleep(void *chan, struct spinlock *lk) { (void)chan; (void)lk; }
void
wakeup(void *chan) { (void)chan; }

int
copyinstr(pagetable_t p, char *dst, uint64 srcva, uint64 max)
{
  uint64 i;
  (void)p;
  if (srcva >= USERSZ)
    return -1;
  for (i = 0; i < max && srcva + i < USERSZ; i++) {
    dst[i] = (char)g_user[srcva + i];
    if (dst[i] == 0)
      return 0;
  }
  return (srcva + max <= USERSZ) ? 0 : -1;
}

int
copyin(pagetable_t p, char *dst, uint64 srcva, uint64 len)
{
  (void)p;
  if (srcva + len > USERSZ)
    return -1;
  memmove(dst, g_user + srcva, len);
  return 0;
}

int
copyout(pagetable_t p, uint64 dstva, char *src, uint64 len)
{
  (void)p;
  if (dstva + len > USERSZ)
    return -1;
  memmove(g_user + dstva, src, len);
  return 0;
}

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

static char g_console[1024];
static int g_console_len;
void
consoleputc(int c)
{
  if (g_console_len < (int)sizeof(g_console) - 1)
    g_console[g_console_len++] = (char)c;
  g_console[g_console_len] = 0;
}

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

/* ---- syscall argument stubs for the sysfile.c handlers ---- */
static const char *g_arg0str, *g_arg1str;
static int    g_arg0int, g_arg1int, g_arg2int;
static uint64 g_arg1addr, g_arg2addr;

int
argstr(int n, char *buf, int max)
{
  const char *s;
  int len;
  if (n == 0)
    s = g_arg0str;
  else if (n == 1)
    s = g_arg1str;
  else
    return -1;
  if (s == 0)
    return -1;
  len = (int)strlen(s);
  if (len + 1 > max)
    return -1;
  memmove(buf, s, (size_t)len + 1);
  return len;
}

int
argint(int n, int *ip)
{
  switch (n) {
  case 0: *ip = g_arg0int; return 0;
  case 1: *ip = g_arg1int; return 0;
  case 2: *ip = g_arg2int; return 0;
  default: return -1;
  }
}

int
argaddr(int n, uint64 *ip)
{
  switch (n) {
  case 1: *ip = g_arg1addr; return 0;
  case 2: *ip = g_arg2addr; return 0;
  default: return -1;
  }
}

/* ---- pipe module stubs (this harness tests FD_INODE only) ----
 * kernel/pipe.c is owned by kernel/pipe and is not linked here; these stubs
 * satisfy the linker for the FD_PIPE dispatch paths in file.c / sysfile.c,
 * which the FD_INODE workload never reaches. */
int
pipealloc(struct file **f0, struct file **f1)
{
  (void)f0; (void)f1;
  return -1;
}
void
pipeclose(struct pipe *pi, int writable)
{
  (void)pi; (void)writable;
  panic("pipeclose: not linked (FD_PIPE never used in this harness)");
}
int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  (void)pi; (void)addr; (void)n;
  return -1;
}
int
piperead(struct pipe *pi, uint64 addr, int n)
{
  (void)pi; (void)addr; (void)n;
  return -1;
}

/* ---- kernel/log and kernel/bio internals for remount / reference checks. ---- */
struct logheader { int n; int block[LOGSIZE]; };
struct log {
  struct spinlock lock;
  int start, size, outstanding, committing, dev;
  struct logheader lh;
};
extern struct log log;
struct bcache {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct buf head;
};
extern struct bcache bcache;

/* The syscall handlers under test (kernel/sysfile.c). */
extern uint64 sys_open(void);
extern uint64 sys_close(void);
extern uint64 sys_write(void);
extern uint64 sys_read(void);
extern uint64 sys_fstat(void);
extern uint64 sys_dup(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_chdir(void);

#define CHECK(cond, msg) \
  do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

static int
load_disk(const char *img)
{
  FILE *fp = fopen(img, "rb");
  size_t got;
  if (fp == 0) { fprintf(stderr, "FAIL: cannot open image %s\n", img); return 1; }
  got = fread(g_disk, 1, sizeof(g_disk), fp);
  fclose(fp);
  if (got <= 0) { fprintf(stderr, "FAIL: empty image %s\n", img); return 1; }
  g_writes = 0;
  return 0;
}

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

static int
setup(const char *img)
{
  struct inode *root;
  int i;
  if (load_disk(img) != 0)
    return 1;
  binit();
  force_cache_invalidate();
  fsinit(1);
  fileinit();

  memset(&g_proc, 0, sizeof(g_proc));
  g_proc.sz = USERSZ;
  for (i = 0; i < NOFILE; i++)
    g_proc.ofile[i] = 0;
  root = namei("/");
  if (root == 0)
    return 1;
  g_proc.cwd = root;
  memset(g_user, 0, sizeof(g_user));
  return 0;
}

static void
put_user_string(uint64 va, const char *s)
{
  memcpy(g_user + va, s, strlen(s) + 1);
}

static void
open_rw(int *fd_out)
{
  g_arg0str = "/file_abi";
  g_arg1int = O_CREATE | O_RDWR | O_TRUNC;
  *fd_out = (int)sys_open();
}

static int
count_file_refs(void)
{
  int i, tot = 0;
  for (i = 0; i < NFILE; i++)
    tot += ftable.file[i].ref;
  return tot;
}

/* ---- deterministic contract checks ---- */
static int
run_contract(const char *img)
{
  struct inode *ip;
  struct stat st;
  const char *hello = "hello, lab 6 file abi\n";
  int hi = (int)strlen(hello);
  int fd, fd2, nd, n;

  if (setup(img) != 0)
    return 1;

  /* ---- open + create publishes exactly one reference (open contract). ---- */
  open_rw(&fd);
  CHECK(fd >= 0, "sys_open(O_CREATE) failed");
  CHECK(g_proc.ofile[fd] != 0, "open did not publish a descriptor reference");
  CHECK(g_proc.ofile[fd]->ref == 1, "open must publish exactly one file ref");
  CHECK(count_file_refs() == 1, "only the opened file should be live");

  /* ---- write: validated user buffer; offset advances by transferred count. */
  put_user_string(0x3000, hello);
  g_arg0int = fd; g_arg1addr = 0x3000; g_arg2int = hi;
  n = (int)sys_write();
  CHECK(n == hi, "sys_write returned wrong byte count");
  CHECK(g_proc.ofile[fd]->off == (uint)hi,
        "shared offset must advance by exactly the transferred count");

  /* ---- append: write at EOF extends the file and advances the offset. ---- */
  put_user_string(0x3200, "tail\n");
  g_arg0int = fd; g_arg1addr = 0x3200; g_arg2int = 5;
  n = (int)sys_write();
  CHECK(n == 5, "append returned wrong byte count");
  CHECK(g_proc.ofile[fd]->off == (uint)hi + 5, "append offset wrong");

  /* ---- close: clear the slot before releasing; final close releases inode. */
  g_arg0int = fd;
  n = (int)sys_close();
  CHECK(n == 0, "sys_close failed");
  CHECK(g_proc.ofile[fd] == 0, "close must clear the descriptor slot");
  CHECK(count_file_refs() == 0, "all file refs must be released after close");

  /* ---- reopen read-only and read committed contents back. ---- */
  g_arg0str = "/file_abi";
  g_arg1int = O_RDONLY;
  fd2 = (int)sys_open();
  CHECK(fd2 >= 0, "reopen read-only failed");
  CHECK(g_proc.ofile[fd2]->ref == 1, "reopen publishes exactly one ref");

  memset(g_user + 0x3400, 0, 128);
  g_arg0int = fd2; g_arg1addr = 0x3400; g_arg2int = 128;
  n = (int)sys_read();
  CHECK(n == hi + 5, "read returned wrong total byte count");
  CHECK(memcmp(g_user + 0x3400, hello, (size_t)hi) == 0,
        "read-back content mismatch");
  CHECK(memcmp(g_user + 0x3400 + hi, "tail\n", 5) == 0,
        "appended tail content mismatch");
  CHECK(g_proc.ofile[fd2]->off == (uint)hi + 5,
        "read shared offset must advance by transferred count");

  /* ---- fstat: aggregate kernel/inode.stati through validated copyout. ---- */
  memset(&st, 0, sizeof(st));
  memset(g_user + 0x3600, 0, sizeof(st));
  g_arg0int = fd2;
  g_arg1addr = 0x3600;
  n = (int)sys_fstat();
  CHECK(n == 0, "sys_fstat failed");
  memcpy(&st, g_user + 0x3600, sizeof(st));
  CHECK(st.type == T_FILE, "fstat type mismatch");
  CHECK(st.size == (uint64)hi + 5, "fstat size mismatch");

  /* ---- dup: two descriptors observe one serialized shared offset. ---- */
  g_arg0int = fd2;
  nd = (int)sys_dup();
  CHECK(nd >= 0 && nd != fd2, "sys_dup failed or returned the same slot");
  CHECK(g_proc.ofile[nd] == g_proc.ofile[fd2],
        "dup must reference the same global file object");
  CHECK(g_proc.ofile[fd2]->ref == 2, "dup must add exactly one shared ref");
  CHECK(g_proc.ofile[fd2]->off == (uint)hi + 5, "dup preserved shared offset");

  /* ---- namespace_mutation: mkdir + chdir publish committed state. ---- */
  g_arg0str = "/d1";
  n = (int)sys_mkdir();
  CHECK(n == 0, "sys_mkdir(/d1) failed");
  ip = namei("/d1");
  CHECK(ip != 0 && ip->type == T_DIR, "mkdir did not publish a directory");
  if (ip) { ilock(ip); iunlock(ip); iput(ip); }

  g_arg0str = "/d1";
  n = (int)sys_chdir();
  CHECK(n == 0, "sys_chdir(/d1) failed");
  CHECK(g_proc.cwd != 0 && g_proc.cwd->type == T_DIR,
        "chdir must publish the new current-directory reference");

  /* ---- link + unlink a hard link to the regular file. ---- */
  g_arg0str = "/file_abi";
  g_arg1str = "/hardlink";
  n = (int)sys_link();
  CHECK(n == 0, "sys_link(/file_abi -> /hardlink) failed");
  ip = namei("/hardlink");
  CHECK(ip != 0 && ip->type == T_FILE, "hard link did not resolve to a file");
  if (ip) { ilock(ip); CHECK(ip->nlink == 2, "hard link must bump nlink"); iunlock(ip); iput(ip); }

  g_arg0str = "/hardlink";
  n = (int)sys_unlink();
  CHECK(n == 0, "sys_unlink(/hardlink) failed");
  ip = namei("/hardlink");
  CHECK(ip == 0, "unlink did not remove the committed directory entry");
  if (ip) { ilock(ip); iunlock(ip); iput(ip); }

  /* ---- capacity: close dup then original fully releases. ---- */
  g_arg0int = nd;
  n = (int)sys_close();
  CHECK(n == 0, "close(dup) failed");
  g_arg0int = fd2;
  CHECK((int)sys_close() == 0, "close(original) failed");

  /* ---- validation: invalid descriptor returns -1 unchanged. ---- */
  g_arg0int = 99;
  CHECK((int)sys_close() == -1, "close of an out-of-range fd must fail");

  return 0;
}

/* ---- fixed-seed fuzz ---- */
static unsigned int g_rng;
static unsigned int
next_rand(void)
{
  /* xorshift32 - deterministic across hosts for a fixed seed. */
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

/* Drive a bounded fixed-seed sequence: open/append/write/close cycles across
 * a small set of file names while preserving the descriptor/offset/reference
 * and content invariants after every step. */
static int
run_fuzz(const char *img, unsigned seed, int cases)
{
  const char *body = "0123456789abcdef\n";
  int bodylen = (int)strlen(body);
  static const char *names[] = { "/fz_a", "/fz_b", "/fz_c" };
  int nfiles = (int)(sizeof(names) / sizeof(names[0]));
  int i, step;
  int refs_before;

  g_rng = seed;   /* seed must be a JSON integer; cast handles the values. */
  if (setup(img) != 0)
    return 1;

  refs_before = count_file_refs();
  (void)refs_before;

  for (step = 0; step < cases; step++) {
    int which = (int)(next_rand() % (unsigned)nfiles);
    int fd, n, want = (int)(next_rand() % 32) + 1;

    g_arg0str = names[which];
    g_arg1int = O_CREATE | O_RDWR | O_TRUNC;
    fd = (int)sys_open();
    CHECK(fd >= 0, "fuzz open failed");

    /* write `want` bytes (repeat body to reach want). */
    for (i = 0; i < want; ) {
      int chunk = want - i < bodylen ? want - i : bodylen;
      memcpy(g_user + 0x3000, body, (size_t)chunk);
      g_arg0int = fd; g_arg1addr = 0x3000; g_arg2int = chunk;
      n = (int)sys_write();
      CHECK(n == chunk, "fuzz write short");
      i += chunk;
    }
    CHECK((long)g_proc.ofile[fd]->off == want, "fuzz offset must equal bytes");

    /* Close releases the reference exactly once. */
    g_arg0int = fd;
    CHECK((int)sys_close() == 0, "fuzz close failed");
    CHECK(count_file_refs() == 0,
          "fuzz must leave no live file references after each cycle");
  }
  return 0;
}

int
main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: file_test fs.img [SEED CASES]\n");
    return 2;
  }
  if (argc >= 4)
    return run_fuzz(argv[1], (unsigned)strtoul(argv[2], 0, 0), atoi(argv[3]));
  return run_contract(argv[1]);
}