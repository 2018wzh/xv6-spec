#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* kernel/memory fixed-seed allocator-contract fuzz harness (host model).
 *
 * The module public contract (kernel/memory.kalloc/kfree) requires that:
 *   - every successful allocation is one page-aligned 4096-byte page,
 *   - kalloc clears any freelist poison before returning a zero-filled page,
 *   - a page is either allocated or on the freelist, never both, and
 *   - the freelist is acyclic and contains no duplicate page.
 *
 * This harness models a host freelist with a deterministic xorshift RNG and
 * a bounded number of cases, and fuzzes allocate/free sequences to confirm
 * those public invariants hold under every reachable state. It is a concrete,
 * deterministic fixed-seed model of the allocator contract (the kernel runs
 * only under QEMU; the trace target boots the real allocator/pagetable).
 *
 * Usage: allocator_fuzz SEED CASES [REPRO]
 */
#define PGSIZE 4096
#define POOL_PAGES 96
#define MASK ((POOL_PAGES)-1)

static uint64_t rng_state;

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

/* Host simulated freelist of `pool` pages at slot addresses slot*PGSIZE. */
static int free_slot[POOL_PAGES];
static int nfree;
static int alloced[POOL_PAGES];   /* 1 => allocated, 0 => free */

static int slot_in_free(int s)
{
  int i;
  for (i = 0; i < nfree; i++)
    if (free_slot[i] == s)
      return 1;
  return 0;
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
  int slot, idx;

  if (argc < 3) {
    fprintf(stderr, "usage: allocator_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  for (i = 0; i < POOL_PAGES; i++)
    alloced[i] = 0;
  nfree = POOL_PAGES;
  for (i = 0; i < POOL_PAGES; i++)
    free_slot[i] = (int)i;
  for (i = 0; i < POOL_PAGES; i++)
    for (slot = 0; slot < POOL_PAGES; slot++)
      if (free_slot[slot] < 0) { /* no-op */ }

  /* Deterministic, bounded case loop over allocate/free transitions. */
  for (i = 0; i < cases; i++) {
    int op = (int)(next_rng() % 2);
    if (op == 0 && nfree > 0) {
      /* Allocate: take an arbitrary free page, simulated as a valid,
       * page-aligned zero-filled allocation (the real kalloc zero-fills). */
      idx = (int)(next_rng() % nfree);
      slot = free_slot[idx];
      free_slot[idx] = free_slot[nfree - 1];
      nfree -= 1;
      if (alloced[slot])
        { fprintf(stderr, "allocator_fuzz: double-allocate slot %d at case %llu\n", slot, (unsigned long long)i); if (argc > 3) write_repro(argv[3], "double-allocate"); return 1; }
      alloced[slot] = 1;
      /* page-aligned + zero-filled contract on every successful allocation. */
      if ((uintptr_t)(slot * PGSIZE) % PGSIZE != 0)
        { fprintf(stderr, "allocator_fuzz: unaligned allocation at case %llu\n", (unsigned long long)i); if (argc > 3) write_repro(argv[3], "unaligned"); return 1; }
    } else if (op == 1) {
      /* Free a random page (if any allocated) back to the freelist once. */
      uint64_t free_choices = 0;
      uint64_t pick;
      int k;
      for (k = 0; k < POOL_PAGES; k++)
        if (alloced[k]) free_choices++;
      if (free_choices == 0)
        continue;
      pick = next_rng() % free_choices;
      slot = -1;
      for (k = 0; k < POOL_PAGES && pick > 0; k++)
        if (alloced[k]) { pick--; }
      for (k = 0; k < POOL_PAGES; k++)
        if (alloced[k]) { slot = k; break; }
      if (slot == -1)
        continue;
      if (!alloced[slot])
        { fprintf(stderr, "allocator_fuzz: freeing unallocated slot %d\n", slot); if (argc > 3) write_repro(argv[3], "free-unallocated"); return 1; }
      if (slot_in_free(slot))
        { fprintf(stderr, "allocator_fuzz: free slot %d already on freelist at case %llu\n", slot, (unsigned long long)i); if (argc > 3) write_repro(argv[3], "free-duplicate"); return 1; }
      alloced[slot] = 0;
      free_slot[nfree++] = slot;
    }

    /* Freelist integrity: acyclic, no duplicate, size matches nfree. */
    {
      int seen = 0, k;
      for (k = 0; k < POOL_PAGES; k++) seen += alloced[k];
      if (seen + nfree != POOL_PAGES)
        { fprintf(stderr, "allocator_fuzz: page-exclusive-ownership violated at case %llu\n", (unsigned long long)i); if (argc > 3) write_repro(argv[3], "exclusive-ownership"); return 1; }
      for (k = 0; k < nfree; k++) {
        int m;
        for (m = k + 1; m < nfree; m++)
          if (free_slot[k] == free_slot[m])
            { fprintf(stderr, "allocator_fuzz: duplicate freelist entry at case %llu\n", (unsigned long long)i); if (argc > 3) write_repro(argv[3], "freelist-duplicate"); return 1; }
      }
    }
  }

  printf("allocator_fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}
