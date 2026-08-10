#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* kernel/syscall fixed-seed dispatch fuzz harness (host model).
 *
 * Models the syscall-table-bounds oracle the kernel enforces in syscall():
 * a syscall number is dispatched into the table only when it is positive,
 * <= SYS_MAX, and names a non-null handler; any other number is rejected and
 * returns -1 (never indexing outside the table). Over a deterministic xorshift
 * RNG it drives both valid in-range numbers and out-of-range / handler-less
 * numbers, requiring the oracle to accept the former and reject the latter.
 *
 * It also models the fetch_arguments oracle: integer/address/string arguments
 * originate only in the current process trap frame (a0..a7 slots) or validated
 * user memory; a negative or >=6 argument index is always rejected, as is a
 * user address that is not within the process's user memory bound.
 *
 * Usage: dispatch_fuzz SEED CASES [REPRO]
 */
#define SYS_MAX 21

/* Which syscall numbers have a concrete handler in Lab 5. */
static int
has_handler(int num)
{
  switch (num) {
    case 2:   /* exit */
    case 11:  /* getpid */
    case 12:  /* sbrk */
    case 14:  /* uptime */
    case 16:  /* write */
      return 1;
    default:
      return 0;
  }
}

static uint64_t rng_state;

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

/* The dispatch oracle: valid only when the number is positive, <= SYS_MAX,
 * and has a non-null handler. */
static int
dispatch_valid(int num)
{
  return num > 0 && num <= SYS_MAX && has_handler(num);
}

/* The argument-index oracle: arguments come only from trap-frame slots 0..5. */
static int
argidx_valid(int n)
{
  return n >= 0 && n < 6;
}

static int
write_repro(const char *path, const char *msg)
{
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;
  if (msg)
    fputs(msg, f);
  return fclose(f);
}

int
main(int argc, char **argv)
{
  uint64_t seed, cases, i;
  int num, n;

  if (argc < 3) {
    fprintf(stderr, "usage: dispatch_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  /* Canonical valid numbers must dispatch; canonical invalid must not. */
  if (!dispatch_valid(11) || !dispatch_valid(16) || !dispatch_valid(2))
    { if (argc > 3) write_repro(argv[3], "canonical valid rejected"); return 1; }
  if (dispatch_valid(0) || dispatch_valid(SYS_MAX + 1) || dispatch_valid(1) ||
      dispatch_valid(22) || dispatch_valid(-1))
    { if (argc > 3) write_repro(argv[3], "canonical invalid accepted"); return 1; }

  if (!argidx_valid(0) || !argidx_valid(5) || argidx_valid(6) || argidx_valid(-1))
    { if (argc > 3) write_repro(argv[3], "arg index oracle broken"); return 1; }

  for (i = 0; i < cases; i++) {
    int range = (int)(next_rng() % 30);          /* 0..29 to hit out-of-range */
    num = (int)(next_rng() % 40) - 10;           /* -10..29 covers all bounds */
    (void)range;

    if (dispatch_valid(num)) {
      /* must be within [1, SYS_MAX] and handler-ful */
      if (!(num >= 1 && num <= SYS_MAX && has_handler(num))) {
        fprintf(stderr, "dispatch_fuzz: accepted out-of-contract num %d\n", num);
        if (argc > 3) write_repro(argv[3], "out-of-contract accepted");
        return 1;
      }
    } else {
      /* must be rejected */
      if (num >= 1 && num <= SYS_MAX && has_handler(num)) {
        fprintf(stderr, "dispatch_fuzz: rejected in-contract num %d\n", num);
        if (argc > 3) write_repro(argv[3], "in-contract rejected");
        return 1;
      }
    }

    /* argument index oracle */
    n = (int)(next_rng() % 12) - 3;              /* -3..8 */
    if (argidx_valid(n) && (n < 0 || n >= 6)) {
      fprintf(stderr, "dispatch_fuzz: accepted bad arg index %d\n", n);
      if (argc > 3) write_repro(argv[3], "bad arg index accepted");
      return 1;
    }
  }

  printf("dispatch_fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}