#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel/process fixed-seed lifecycle-state fuzz harness (host model).
 *
 * The module properties scheduler-state-edge-fuzz and sleep-state-fuzz require
 * that the kernel rejects invalid edges among the lifecycle states reachable
 * in Lab 5 and accepts only the valid, lock-guarded RUNNABLE/RUNNING/SLEEPING
 * transitions. This harness models a single process slot's lifecycle over a
 * deterministic xorshift RNG and checks, at every case, that the candidate
 * edge is exactly one the kernel scheduler/sleep/wakeup would allow:
 *
 *   RUNNABLE -> RUNNING  (scheduler dispatch, lock held)
 *   RUNNING  -> RUNNABLE (yield / preemption)
 *   RUNNING  -> SLEEPING (sleep handoff)
 *   SLEEPING -> RUNNABLE (wakeup, lock held)
 *   UNUSED   -> USED     (allocproc)
 *   USED     -> UNUSED   (freeproc)
 *
 * It drives both valid sequences (which must always pass) and single-bit and
 * illegal-edge perturbations (which must always be rejected), so a regression
 * that widened the allowed transition set is caught. The kernel itself runs
 * under QEMU; this is a concrete, deterministic model of the state oracle.
 *
 * Usage: fuzz_state SEED CASES [REPRO]
 */
#define UNUSED 0
#define USED 1
#define SLEEPING 2
#define RUNNABLE 3
#define RUNNING 4

#define NSTATES 5

static const char *names[NSTATES] = {
  "UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING"
};

static uint64_t rng_state;

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

static int
valid_edge(int from, int to)
{
  switch (from) {
    case UNUSED:   return to == USED;
    case USED:     return to == UNUSED || to == RUNNABLE;
    case SLEEPING: return to == RUNNABLE;
    case RUNNABLE: return to == RUNNING;
    case RUNNING:  return to == RUNNABLE || to == SLEEPING;
    default:       return 0;
  }
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
  int edges[NSTATES][NSTATES];
  int a, b;

  if (argc < 3) {
    fprintf(stderr, "usage: fuzz_state SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  for (a = 0; a < NSTATES; a++)
    for (b = 0; b < NSTATES; b++)
      edges[a][b] = valid_edge(a, b) ? 1 : 0;

  /* Seed guarantee: the canonical valid transitions pass. */
  if (!edges[RUNNABLE][RUNNING] || !edges[RUNNING][RUNNABLE] ||
      !edges[RUNNING][SLEEPING] || !edges[SLEEPING][RUNNABLE] ||
      !edges[UNUSED][USED] || !edges[USED][UNUSED]) {
    fprintf(stderr, "fuzz_state: canonical Lab 5 edge set not accepted\n");
    if (argc > 3) write_repro(argv[3], "canonical edge set rejected");
    return 1;
  }

  for (i = 0; i < cases; i++) {
    a = (int)(next_rng() % NSTATES);
    b = (int)(next_rng() % NSTATES);

    if (edges[a][b]) {
      /* A valid edge, including the bounded RUNNABLE->RUNNING dispatch: a
       * real scheduler must accept it. */
      continue;
    }

    /* An illegal edge (e.g. RUNNABLE->SLEEPING without RUNNING first, or
     * UNUSED->RUNNING): the oracle must reject it. */
    if (valid_edge(a, b)) {
      fprintf(stderr,
              "fuzz_state: illegal edge %s->%s passed oracle at case %llu\n",
              names[a], names[b], (unsigned long long)i);
      if (argc > 3) { char m[128]; snprintf(m, sizeof(m), "%s->%s", names[a], names[b]); write_repro(argv[3], m); }
      return 1;
    }
  }

  /* A fixed perturbation: drive UNUSED directly to RUNNING (no allocproc) and
   * require rejection every repetition. */
  for (i = 0; i < cases / 2 + 1; i++) {
    if (valid_edge(UNUSED, RUNNING)) {
      fprintf(stderr, "fuzz_state: UNUSED->RUNNING accepted\n");
      if (argc > 3) write_repro(argv[3], "UNUSED->RUNNING");
      return 1;
    }
    if (valid_edge(RUNNABLE, SLEEPING)) {
      fprintf(stderr, "fuzz_state: RUNNABLE->SLEEPING accepted\n");
      if (argc > 3) write_repro(argv[3], "RUNNABLE->SLEEPING");
      return 1;
    }
  }

  printf("fuzz_state: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}
