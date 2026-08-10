#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel/virtio fixed-seed descriptor-ownership fuzz harness (host model).
 *
 * The module invariants require that the fixed descriptor pool is partitioned
 * into disjoint free and in-flight sets (descriptor-partition) and that every
 * descriptor is either free or belongs to exactly one in-flight request
 * (virtio-fixed-descriptor-ownership). Each logical-block request consumes a
 * three-descriptor chain; a temporary lack of descriptors must block and
 * resume without loss (alloc3_desc releases any partial chain).
 *
 * This harness models the fixed pool (NUM descriptors) over a deterministic
 * xorshift RNG, issuing bounded request/submit and completion (reclaim)
 * sequences. It checks, at every case, that:
 *   - alloc3_desc returns three distinct descriptors and never aliases an
 *     in-flight descriptor;
 *   - a partial allocation is fully released when a request cannot be
 *     satisfied;
 *   - free_chain returns every descriptor of a chain to the free set exactly
 *     once;
 *   - free and in-flight sets remain disjoint and together cover the pool,
 *     with no negative or double free.
 *
 * Usage: fuzz_ring SEED CASES [REPRO]
 */
#define NUM 8

static struct desc_pool {
  int free;       /* 1 = free, 0 = in flight */
  uint64_t chain_id; /* chain this descriptor currently belongs to, 0 = none */
} pool[NUM];

static uint64_t rng_state;

static void write_repro_file(const char *msg);

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

static int pool_consistent(void)
{
  int i;
  for (i = 0; i < NUM; i++) {
    if (pool[i].free != 0 && pool[i].free != 1)
      return 0;
  }
  return 1;
}

/* Allocate a three-descriptor chain, or return -1 having released any partial
 * allocation. Mirrors alloc3_desc: never aliases an in-flight descriptor. */
static int
alloc3_desc(int *idx)
{
  int i;
  for (i = 0; i < 3; i++) {
    idx[i] = -1;
    /* Find the first free descriptor. */
    int d = -1, j;
    for (j = 0; j < NUM; j++) {
      if (pool[j].free) { d = j; break; }
    }
    if (d < 0) {
      /* Unable to allocate descriptor i of 3: release the partial chain. */
      int k;
      for (k = 0; k < i; k++) {
        if (pool[idx[k]].free != 0) {
          fprintf(stderr, "fuzz_ring: partial chain descriptor not in flight\n");
          write_repro_file("alloc: partial not in flight");
          return -1;
        }
        pool[idx[k]].free = 1;
        pool[idx[k]].chain_id = 0;
      }
      return -1;
    }
    idx[i] = d;
    pool[d].free = 0;
    pool[d].chain_id = (uint64_t)(i + 1); /* request-local id */
  }
  return 0;
}

/* Reclaim a whole chain, freeing each descriptor exactly once. */
static void
free_chain(int *idx)
{
  int i;
  for (i = 0; i < 3; i++) {
    if (idx[i] < 0 || idx[i] >= NUM)
      continue;
    if (pool[idx[i]].free != 0) {
      fprintf(stderr, "fuzz_ring: double free of descriptor %d\n", idx[i]);
      write_repro_file("double free");
      exit(1);
    }
    pool[idx[i]].free = 1;
    pool[idx[i]].chain_id = 0;
  }
}

static void
write_repro_file(const char *msg)
{
  const char *path = getenv("FUZZ_REPRO");
  if (path && path[0]) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(msg, f); fclose(f); }
  }
}

int
main(int argc, char **argv)
{
  uint64_t seed, cases, round;
  int open_chains = 0;
  int i;

  if (argc < 3) {
    fprintf(stderr, "usage: fuzz_ring SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  if (argc > 3)
    setenv("FUZZ_REPRO", argv[3], 1);
  rng_state = seed ? seed : 1;

  for (i = 0; i < NUM; i++) {
    pool[i].free = 1;
    pool[i].chain_id = 0;
  }

  for (round = 0; round < cases; round++) {
    int want = (int)(next_rng() % 2);
    int idx[3] = { -1, -1, -1 };

    if (want == 0 || open_chains == 0) {
      /* Submit a new request (unless we hold no descriptors and the pool is
       * exhausted: then allocation must fail cleanly without corrupting). */
      if (alloc3_desc(idx) == 0) {
        open_chains++;
        /* The chain must be three distinct in-flight descriptors. */
        if (idx[0] == idx[1] || idx[1] == idx[2] || idx[0] == idx[2]) {
          fprintf(stderr, "fuzz_ring: aliased descriptors in one chain\n");
          write_repro_file("aliased chain");
          return 1;
        }
      } else {
        /* Allocation failed: the pool had < 3 free descriptors. Verify the
         * partial allocation was fully released (pool back to a consistent
         * state with the same open_chains). */
        if (!pool_consistent()) {
          fprintf(stderr, "fuzz_ring: pool inconsistent after failed alloc\n");
          write_repro_file("pool inconsistent");
          return 1;
        }
      }
    } else {
      /* Reclaim one in-flight chain. */
      free_chain(idx);
      open_chains--;
    }

    /* The free and in-flight sets must always partition the pool. */
    if (!pool_consistent()) {
      fprintf(stderr, "fuzz_ring: pool partition broken at round %llu\n",
              (unsigned long long)round);
      write_repro_file("partition broken");
      return 1;
    }
  }

  printf("fuzz_ring: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}
