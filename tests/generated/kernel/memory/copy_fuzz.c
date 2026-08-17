#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel/memory validated user-copy fuzz harness (host model).
 *
 * The module contract (kernel/memory.copyin/copyout/copyinstr) requires,
 * without a raw user-pointer dereference, that:
 *   - a range touching an unmapped user page or starting at/above MAXVA
 *     returns failure,
 *   - copyinstr copies through the first NUL within the finite bound and
 *     returns failure on an unterminated string or an unmapped page, and
 *   - on success every requested byte is copied (copyin/copyout) or the
 *     destination is NUL-terminated within the bound (copyinstr).
 *
 * This harness models a small user page table (a bitmap of mapped pages) and
 * a byte array forming user memory, then replays the same page-walk
 * validate-then-copy logic the kernel uses. An independent, byte-by-byte
 * naive oracle classifies each input as success/failure so the model is
 * checked against a second implementation, not against itself. It is a
 * concrete deterministic fixed-seed model; the kernel runs only under QEMU so
 * the real copies are additionally covered by the source contract and, in a
 * later lab, the syscall argument tests.
 *
 * Usage: copy_fuzz SEED CASES [REPRO]
 */
#define PGSIZE 4096
#define PAGES  64                          /* 64 user pages = 256 KiB user space */
#define USERSPACE ((uint64_t)PAGES * PGSIZE)
#define MAXVA  (UINT64_C(1) << 50)         /* Sv39 top boundary, as in riscv.h  */

static uint64_t rng_state;
static uint8_t  mapped[PAGES];
static uint8_t  mem[USERSPACE];

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

/* naive truth about whether the entire half-open byte range [va, va+len)
 * is mapped and below both MAXVA and the model's user-space bound. */
static int range_fully_mapped(uint64_t va, uint64_t len)
{
  uint64_t v;
  if (va >= MAXVA)
    return 0;
  if (va + len > MAXVA)
    return 0;
  for (v = va; v < va + len; v++) {
    uint64_t idx = v >> 12;
    if (idx >= PAGES || !mapped[idx])
      return 0;
  }
  return 1;
}

/* kernel-equivalent walkaddr: -1 if unmapped or >= MAXVA, else backing idx. */
static long walkaddr_model(uint64_t va)
{
  uint64_t idx;
  if (va >= MAXVA)
    return -1;
  idx = va >> 12;
  if (idx >= PAGES || !mapped[idx])
    return -1;
  return (long)(idx * PGSIZE + (va & (PGSIZE - 1)));
}

/* model copyin: return 1 success / 0 failure. */
static int copyin_model(uint64_t srcva, uint64_t len, uint8_t *dst)
{
  while (len > 0) {
    long off = walkaddr_model(srcva);
    uint64_t n;
    if (off < 0)
      return 0;
    n = PGSIZE - (srcva & (PGSIZE - 1));
    if (n > len)
      n = len;
    memcpy(dst, mem + off, (size_t)n);
    dst += n;
    srcva += n;
    len -= n;
  }
  return 1;
}

/* model copyinstr: return number of bytes (incl NUL) or -1 failure. */
static long copyinstr_model(uint64_t srcva, uint64_t max, uint8_t *dst)
{
  uint64_t done = 0;
  while (done < max) {
    long off = walkaddr_model(srcva + done);
    if (off < 0)
      return -1;
    {
      uint8_t b = mem[off];
      dst[done++] = b;
      if (b == 0)
        return (long)done;
    }
  }
  return -1;   /* bound exhausted with no NUL */
}

static int write_repro(const char *path, const char *msg)
{
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;
  if (msg)
    fputs(msg, f);
  return fclose(f);
}

int main(int argc, char **argv)
{
  uint64_t seed, cases, i;
  uint8_t dst[8 * PGSIZE];

  if (argc < 3) {
    fprintf(stderr, "usage: copy_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  for (i = 0; i < PAGES; i++)
    mapped[i] = 0;

  for (i = 0; i < cases; i++) {
    uint64_t op, len, max, va, v;
    long r;
    int k;

    /* 60% of cases toggle one page's map to keep the table interesting. */
    if ((next_rng() % 100) < 60) {
      k = (int)(next_rng() % PAGES);
      mapped[k] = mapped[k] ? 0 : 1;
    }
    memset(mem, (int)(next_rng() & 0xff), sizeof(mem));

    op = next_rng() % 3;
    if ((next_rng() % 8) == 0)
      va = MAXVA + (next_rng() % (1 << 20));  /* force an out-of-range start */
    else
      va = next_rng() % (USERSPACE + 1);      /* may sit right at the edge   */

    if (op == 0 || op == 1) {
      /* copyin / copyout share identical range-validation. */
      len = next_rng() % (4 * PGSIZE);
      memset(dst, 0xEE, sizeof(dst));
      r = copyin_model(va, len, dst);
      if (r == 1) {
        /* accepted only when the full range is mapped (or len == 0). */
        if (len != 0 && !range_fully_mapped(va, len)) {
          fprintf(stderr, "copy_fuzz: copy accepted an unmapped/overflowing range (case %llu, va %llu, len %llu)\n",
                  (unsigned long long)i, (unsigned long long)va,
                  (unsigned long long)len);
          if (argc > 3) write_repro(argv[3], "copy-accepted-bad-range");
          return 1;
        }
        /* and the copied bytes must equal the user bytes at every offset. */
        for (v = 0; v < len; v++) {
          if (dst[v] != mem[walkaddr_model(va + v)]) {
            fprintf(stderr, "copy_fuzz: copied byte mismatch at case %llu off %llu\n",
                    (unsigned long long)i, (unsigned long long)v);
            if (argc > 3) write_repro(argv[3], "copy-byte-mismatch");
            return 1;
          }
        }
      } else {
        /* rejected only when some byte is unmapped or the range overflows. */
        if (len == 0 || range_fully_mapped(va, len)) {
          fprintf(stderr, "copy_fuzz: copy rejected a fully-mapped range (case %llu, va %llu, len %llu)\n",
                  (unsigned long long)i, (unsigned long long)va,
                  (unsigned long long)len);
          if (argc > 3) write_repro(argv[3], "copy-rejected-good-range");
          return 1;
        }
      }
    } else {
      /* copyinstr: string copy bounded by max. */
      max = next_rng() % 128;
      memset(dst, 0x5A, sizeof(dst));
      r = copyinstr_model(va, max, dst);
      if (r >= 0) {
        if ((uint64_t)r > max || dst[r - 1] != 0) {
          fprintf(stderr, "copy_fuzz: copyinstr overran or not NUL-terminated (case %llu)\n",
                  (unsigned long long)i);
          if (argc > 3) write_repro(argv[3], "copyinstr-overrun");
          return 1;
        }
      } else {
        /* failure means no NUL found within max mapped bytes. */
        uint64_t scanned = 0;
        int hit_unmapped = 0;
        for (; scanned < max; scanned++) {
          long off = walkaddr_model(va + scanned);
          if (off < 0) {
            hit_unmapped = 1;
            break;
          }
          if (mem[off] == 0)
            break;   /* a mapped NUL exists within the bound */
        }
        if (!hit_unmapped && scanned < max) {
          /* we broke out because we found a NUL -> model should have passed */
          fprintf(stderr, "copy_fuzz: copyinstr failed though NUL in mapped bound (case %llu)\n",
                  (unsigned long long)i);
          if (argc > 3) write_repro(argv[3], "copyinstr-false-failure");
          return 1;
        }
      }
    }
  }

  printf("copy_fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}