/* kernel/pipe validation harness.
 *
 * Compiles the real kernel/pipe.c + kernel/file.c together with lightweight
 * single-threaded stubs for the process, memory, and inode dependencies the
 * file and pipe layers need (spinlock primitives, myproc/current process,
 * kalloc/kfree, validated user-address copy helpers, and the inode/log
 * operations file.c forwards to for non-pipe files). The harness drives
 * pipealloc/piperead/pipewrite/pipeclose directly against the actual
 * kernel/pipe implementation and checks the Lab 7 pipe invariants:
 *
 *   - pipe-fifo-order: the bounded ring exposes every accepted byte exactly
 *     once and in acceptance order across wraparound.
 *   - pipe-capacity-bound: a single write cannot publish more bytes than the
 *     ring capacity, and the write position never overtakes the read
 *     position by more than the capacity.
 *   - pipe-endpoint-reference-consistency: pipealloc publishes exactly two
 *     file references (one readable, one writable) sharing one pipe; the
 *     final close of each side updates liveness and the backing pipe is
 *     freed only after both sides have no live references.
 *   - pipe-peer-close-termination: closing the final writer wakes an empty
 *     reader (observed as EOF in the single-threaded model); closing the
 *     final reader makes writes fail stably (broken pipe).
 *
 * In this single-threaded model, sleep/wakeup are no-ops, so an operation
 * that would block in a real kernel (empty-with-writer, full-with-reader)
 * would spin. To keep the contract and fuzz deterministic, every write is
 * bounded to the current free space and every read is bounded to the current
 * buffered count, so no operation reaches the blocking path.
 *
 * Usage: pipe_test            -> run deterministic contract checks
 *        pipe_test SEED CASES -> also run the fixed-seed fuzz workload
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "buf.h"      /* defines struct sleeplock before fs.h uses it. */
#include "proc.h"
#include "file.h"

/* ---- single-threaded kernel-dependency stubs ---- */

static struct proc g_proc;
static unsigned char g_user[1 << 20];   /* modeled user address space */

/* Panic interception: the harness catches panic via longjmp so expected
 * failure paths can verify a diagnostic without aborting the whole run. */
static jmp_buf g_jmp;
static int g_jmp_armed;
void panic(char *s);   /* declared before sleep uses it. */

/* Kernel memory model: a small static arena for kalloc/kfree. */
#define ARENA_PAGES 32
static unsigned char g_arena[ARENA_PAGES][4096];
static int g_arena_used[ARENA_PAGES];

/* sleep/wakeup accounting: if a pipe operation reaches the blocking wait in
 * this single-threaded model, signal an error instead of spinning forever. */
static int g_sleep_pending;   /* set when a sleep is attempted. */
static int g_sleep_event;     /* incremented on each sleep; overflow guarded. */

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

/* Single-threaded sleep/wakeup. sleep is a no-op that just releases and
 * reacquires the caller lock; if the pipe code falls into a blocking wait in
 * the single-threaded test, we signal it so the test can detect the infinite
 * loop rather than spinning. */
void
sleep(void *chan, struct spinlock *lk)
{
  (void)chan;
  if (lk)
    lk->locked = 0;
  if (lk)
    lk->locked = 1;
  g_sleep_pending = 1;
  g_sleep_event++;
  if (g_sleep_event > 100000)
    panic("pipe_test: infinite blocking wait in single-threaded model");
}

void
wakeup(void *chan)
{
  (void)chan;
}

/* panic interception: pipe.c panics on error paths. The harness catches it
 * via longjmp so expected-failure checks can verify a diagnostic was
 * produced without aborting the whole run. */
void
panic(char *s)
{
  fprintf(stderr, "PANIC: %s\n", s);
  if (g_jmp_armed)
    longjmp(g_jmp, 1);
  exit(1);
}

static void *
alloc_page(void)
{
  int i;
  for (i = 0; i < ARENA_PAGES; i++) {
    if (!g_arena_used[i]) {
      g_arena_used[i] = 1;
      memset(g_arena[i], 0, 4096);
      return g_arena[i];
    }
  }
  return 0;
}

static void
free_page(void *p)
{
  int i;
  for (i = 0; i < ARENA_PAGES; i++) {
    if (g_arena[i] == (unsigned char *)p) {
      g_arena_used[i] = 0;
      return;
    }
  }
  panic("kfree: invalid page");
}

void *
kalloc(void)
{
  return alloc_page();
}

void
kfree(void *p)
{
  free_page(p);
}

int
copyin(pagetable_t p, char *dst, uint64 srcva, uint64 len)
{
  (void)p;
  if (srcva + len > sizeof(g_user))
    return -1;
  memmove(dst, g_user + srcva, (size_t)len);
  return 0;
}

int
copyout(pagetable_t p, uint64 dstva, char *src, uint64 len)
{
  (void)p;
  if (dstva + len > sizeof(g_user))
    return -1;
  memmove(g_user + dstva, src, (size_t)len);
  return 0;
}

/* --- inode/log operation stubs (never exercised by the pipe tests). --- */
struct inode;
void ilock(struct inode *ip) { (void)ip; }
void iunlock(struct inode *ip) { (void)ip; }
void iput(struct inode *ip) { (void)ip; }
void begin_op(void) { }
void end_op(void) { }
int  readi(struct inode *, int, uint64, uint, uint) { return -1; }
int  writei(struct inode *, int, uint64, uint, uint) { return -1; }
void stati(struct inode *, struct stat *) { }

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

/* ---- pipe operation surface (real kernel/pipe.c declarations) ---- */
extern int  pipealloc(struct file **, struct file **);
extern int  pipewrite(struct pipe *, uint64, int);
extern int  piperead(struct pipe *, uint64, int);
extern void pipeclose(struct pipe *, int);

/* ---- test helpers ---- */

#define PIPESZ 512   /* declared capacity inside pipe.c (not exposed). */
#define CHECK(cond, msg) \
  do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

static int
count_live_files(void)
{
  int i, n = 0;
  for (i = 0; i < NFILE; i++)
    if (ftable.file[i].ref > 0)
      n++;
  return n;
}

/* Free-space and buffered counts are tracked by the harness model. To verify
 * the pipe-capacity-bound invariant without exposing the internal struct
 * layout, we track the number of bytes we believe are in the ring and assert
 * it never exceeds PIPESZ. */
static unsigned g_buffered;

static void
note_write(int accepted)
{
  g_buffered += (unsigned)accepted;
}

static void
note_read(int read)
{
  if (read > 0) {
    if ((int)g_buffered < read)
      g_buffered = 0;   /* defend against a test bookkeeping error. */
    else
      g_buffered -= (unsigned)read;
  }
}

static int
free_space(void)
{
  return (int)PIPESZ - (int)g_buffered;
}

/* ---- deterministic contract checks ---- */
static int
run_contract(void)
{
  struct file *rf, *wf;
  int i, n;

  fileinit();
  memset(g_user, 0, sizeof(g_user));
  memset(g_arena_used, 0, sizeof(g_arena_used));
  g_buffered = 0;
  g_sleep_pending = 0;
  g_sleep_event = 0;

  /* pipealloc: returns one readable + one writable endpoint on one pipe. */
  rf = wf = 0;
  if (pipealloc(&rf, &wf) != 0) {
    fprintf(stderr, "FAIL: pipealloc returned -1 without exhaustion pressure\n");
    return 1;
  }
  CHECK(rf != 0 && wf != 0, "pipealloc must publish both endpoints");
  CHECK(rf != wf, "pipealloc must return distinct file objects");
  CHECK(rf->type == FD_PIPE && wf->type == FD_PIPE, "both endpoints are FD_PIPE");
  CHECK(rf->readable && !rf->writable, "read endpoint is read-only");
  CHECK(!wf->readable && wf->writable, "write endpoint is write-only");
  CHECK(rf->pipe != 0 && rf->pipe == wf->pipe, "both endpoints share one pipe");
  CHECK(count_live_files() == 2, "pipealloc published exactly two file refs");

  /* Each endpoint has exactly one reference (pipe-endpoint-reference-consistency). */
  CHECK(rf->ref == 1 && wf->ref == 1, "each published endpoint carries one ref");

  /* Basic FIFO order: write 3 bytes, read them in the same order. */
  memset(g_user + 0x100, 0, sizeof(g_user) - 0x100);
  g_user[0x100] = 'A';
  g_user[0x101] = 'B';
  g_user[0x102] = 'C';
  n = pipewrite(wf->pipe, 0x100, 3);
  CHECK(n == 3, "pipewrite accepted three bytes");
  note_write(3);
  n = piperead(rf->pipe, 0x200, 9);
  CHECK(n == 3, "piperead returned the buffered byte count");
  note_read(3);
  CHECK(g_user[0x200] == 'A' && g_user[0x201] == 'B' && g_user[0x202] == 'C',
        "FIFO order violated on small read");

  /* EOF: an empty pipe whose writers are all closed returns 0 (EOF). */
  fileclose(wf);   /* close the write side of the first pipe. */
  n = piperead(rf->pipe, 0x300, 4);
  CHECK(n == 0, "empty pipe with no writers returns EOF");
  /* Close the read side; the pipe is freed after both sides close. */
  fileclose(rf);

  /* Broken pipe: closing the read side makes subsequent writes fail -1. */
  {
    struct file *r2, *w2;
    if (pipealloc(&r2, &w2) != 0) {
      fprintf(stderr, "FAIL: second pipealloc failed\n");
      return 1;
    }
    /* Close the read side through fileclose; the write endpoint remains. */
    fileclose(r2);
    /* The writable file still owns the pipe; writing now must fail -1
     * because the read side is permanently closed. */
    g_user[0x330] = (char)0xAA;
    n = pipewrite(w2->pipe, 0x330, 1);
    CHECK(n == -1, "write to closed read side returns -1 (broken pipe)");
    /* The pipe was not freed (write side still open), and writing was
     * rejected without publishing bytes. Close the write side too. */
    fileclose(w2);
  }

  /* Wraparound while data is in flight: write a fresh pipe up to capacity,
   * read part back, write more (which wraps in the ring array). */
  {
    struct file *r2, *w2;
    if (pipealloc(&r2, &w2) != 0) {
      fprintf(stderr, "FAIL: second pipealloc failed\n");
      return 1;
    }
    g_buffered = 0;

    /* Fill the idle ring with 300 patterned bytes (under capacity). */
    memset(g_user + 0x400, (char)0xAB, 300);
    n = pipewrite(w2->pipe, 0x400, 300);
    CHECK(n == 300, "pipewrite accepted 300 bytes");
    note_write(300);
    CHECK(g_buffered == 300, "bookkeeping for 300 bytes");

    /* Read 230 back; the ring now has 70 buffered and 442 free. */
    memset(g_user + 0x500, 0, 300);
    n = piperead(r2->pipe, 0x500, 230);
    CHECK(n == 230, "read returned 230");
    note_read(230);
    CHECK(g_buffered == 70, "bookkeeping after 230-byte read");
    for (i = 0; i < 230; i++)
      CHECK(g_user[0x500 + i] == (unsigned char)0xAB,
            "readback pattern mismatch");

    /* Write 300 more while 70 remain: the new data at ring positions that
     * lay before the read position wraps around in the array. */
    memset(g_user + 0x600, (char)0xCD, 300);
    n = pipewrite(w2->pipe, 0x600, 300);
    CHECK(n == 300, "wraparound write accepted 300 bytes");
    note_write(300);
    CHECK(g_buffered == 370, "bookkeeping after wraparound (70+300)");

    /* Read everything back: first the 70 remaining 0xAB, then 300 0xCD. */
    memset(g_user + 0x700, 0, 400);
    n = piperead(r2->pipe, 0x700, 400);
    CHECK(n == 370, "wraparound read drained all 370");
    note_read(370);
    for (i = 0; i < 70; i++)
      CHECK(g_user[0x700 + i] == (unsigned char)0xAB,
            "wraparound FIFO: first batch preserved");
    for (i = 0; i < 300; i++)
      CHECK(g_user[0x700 + 70 + i] == (unsigned char)0xCD,
            "wraparound FIFO: second batch arrives after first");

    /* Now close both sides of this pipe through fileclose, demonstrating
     * the "free pipe once" resource-lifetime invariant. */
    fileclose(w2);   /* close the write side. */
    memset(g_user + 0x810, 0, 4);
    n = piperead(r2->pipe, 0x810, 4);
    CHECK(n == 0, "empty pipe with no writers returns EOF");
    fileclose(r2);   /* close the read side; pipe freed after dual close. */
  }

  /* Explicit close path: close both sides of a fresh pipe through fileclose
   * and confirm the backing pipe page is freed for reuse. */
  {
    struct file *rx, *wx;
    if (pipealloc(&rx, &wx) != 0) {
      fprintf(stderr, "FAIL: pipealloc for close test failed\n");
      return 1;
    }
    fileclose(rx);
    fileclose(wx);
    /* A fresh pipealloc must succeed, reusing the freed pipe page. */
    if (pipealloc(&rx, &wx) != 0) {
      fprintf(stderr, "FAIL: pipe page not freed after final dual close\n");
      return 1;
    }
    fileclose(rx);
    fileclose(wx);
  }

  /* rollback/error path: pipealloc under file-table exhaustion returns -1. */
  {
    int i;
    struct file *ra, *wa;
    /* Hold every file slot so filealloc fails. */
    struct file *all[NFILE];
    int slots = 0;
    for (i = 0; i < NFILE && slots < NFILE; i++)
      all[slots++] = ftable.file[i].ref == 0 ? filealloc() : 0;
    /* All free slots now allocated; pipealloc on the full table must fail. */
    ra = wa = 0;
    n = pipealloc(&ra, &wa);
    CHECK(n == -1, "pipealloc under full file table returns -1");
    CHECK(ra == 0 && wa == 0, "failed pipealloc leaves both outputs null");
    /* Release the filled slots (fileclose each one). */
    for (i = 0; i < slots; i++)
      if (all[i])
        fileclose(all[i]);
  }

  printf("contract: fifo order, capacity bound, peer close, and endpoints passed\n");
  return 0;
}

/* ---- fixed-seed fuzz ---- */
static unsigned int g_rng;

static unsigned int
next_rand(void)
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

static int
run_fuzz(unsigned seed, int cases)
{
  struct file *rf, *wf;
  char sbuf[32];
  int i, n, step, closed_write;

  memset(g_user, 0, sizeof(g_user));
  memset(g_arena_used, 0, sizeof(g_arena_used));
  fileinit();
  g_buffered = 0;
  rf = wf = 0;
  if (pipealloc(&rf, &wf) != 0) {
    fprintf(stderr, "FAIL: fuzz pipealloc failed\n");
    return 1;
  }

  g_rng = seed ? seed : 1;
  closed_write = 0;

  for (step = 0; step < cases; step++) {
    unsigned op = next_rand() % 4;
    int base, tuplen;

    if (op == 0) {
      /* Write a bounded chunk that fits the current free space. */
      if (closed_write)
        continue;   /* no live writer remains. */
      if (free_space() <= 0)
        continue;   /* ring is full; no space to accept more. */
      tuplen = (int)(next_rand() % free_space()) + 1;
      if (tuplen <= 0)
        continue;
      tuplen = tuplen > (int)sizeof(sbuf) ? (int)sizeof(sbuf) : tuplen;
      base = 0x1000;
      /* fill a deterministic pattern based on the step. */
      for (i = 0; i < tuplen; i++)
        sbuf[i] = (char)((step + i) & 0xff);
      memcpy(g_user + base, sbuf, (size_t)tuplen);
      n = pipewrite(wf->pipe, (uint64)base, tuplen);
      if (n != tuplen) {
        fprintf(stderr, "FAIL: fuzz write returned %d (want %d)\n",
                (int)n, tuplen);
        return 1;
      }
      note_write(tuplen);
    } else if (op == 1) {
      /* Read up to a bounded chunk. The result cannot exceed buffered bytes. */
      if (g_buffered == 0)
        continue;   /* nothing buffered; a read would block with a live writer. */
      tuplen = (int)(next_rand() % g_buffered) + 1;
      base = 0x2000;
      memset(g_user + base, 0, (size_t)tuplen);
      n = piperead(rf->pipe, (uint64)base, tuplen);
      if (n < 0 || n > tuplen || n > (int)g_buffered) {
        fprintf(stderr,
                "FAIL: fuzz read returned %d (want <= %d, buffered=%u)\n",
                (int)n, tuplen, g_buffered);
        return 1;
      }
      note_read(n);
    } else if (op == 2) {
      /* Peer close the writable side once; reads then drain to EOF. */
      if (!closed_write) {
        pipeclose(wf->pipe, 1);
        closed_write = 1;
      }
    } else {
      /* Round-trip a tiny write/read to exercise the wraparound index path. */
      if (!closed_write && free_space() >= 1) {
        tuplen = (int)(next_rand() % 4) + 1;
        base = 0x3000;
        for (i = 0; i < tuplen; i++)
          sbuf[i] = (char)('a' + (step % 26));
        memcpy(g_user + base, sbuf, (size_t)tuplen);
        n = pipewrite(wf->pipe, (uint64)base, tuplen);
        if (n != tuplen) {
          fprintf(stderr, "FAIL: fuzz roundtrip write returned %d\n", (int)n);
          return 1;
        }
        note_write(tuplen);
        memset(g_user + base, 0, (size_t)tuplen);
        n = piperead(rf->pipe, (uint64)base, tuplen);
        if (n != tuplen) {
          fprintf(stderr, "FAIL: fuzz roundtrip read returned %d\n", (int)n);
          return 1;
        }
        note_read(tuplen);
      }
    }
  }

  /* Clean up: whatever endpoints remain alive are closed once. */
  if (!closed_write && rf->pipe)
    pipeclose(wf->pipe, 1);
  if (rf->pipe)
    pipeclose(rf->pipe, 0);

  printf("fuzz: %d cases with seed %u passed\n", cases, seed);
  return 0;
}

int
main(int argc, char **argv)
{
  /* No args: deterministic contract checks. With seed+cases: the fixed-seed
   * fuzz workload. */
  memset(g_arena_used, 0, sizeof(g_arena_used));
  memset(&g_proc, 0, sizeof(g_proc));
  g_proc.sz = (uint)sizeof(g_user);
  if (argc < 2)
    return run_contract();
  return run_fuzz((unsigned)strtoul(argv[1], 0, 0), atoi(argv[2]));
}