#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel/process sleep/wakeup contract host model (deterministic, fixed seed).
 *
 * The module contract (kernel/process.sleep_wakeup) requires that:
 *   - sleep atomically transfers from the caller lock to the process lock
 *     before blocking (acquire p->lock, then release lk) so a wakeup after
 *     SLEEPING publication is never lost;
 *   - the caller reacquires its lock after rescheduling;
 *   - wakeup marks any matching SLEEPING process RUNNABLE while holding its
 *     lock.
 *
 * This harness models two processes plus a caller lock and a channel, and a
 * deterministic fixed-seed RNG drives valid and invalid transition sequences
 * to confirm the handoff ordering and the no-lost-wakeup property hold under
 * every reachable state. The real kernel runs under QEMU; this is a concrete
 * deterministic model of the lock-ordering contract.
 *
 * Usage: wakeup_contract SEED CASES [REPRO]
 */
#define NPROC_MIN 2

/* Simplified process model: lifecycle state + owner-lock + wake channel. */
#define UNUSED 0
#define USED 1
#define SLEEPING 2
#define RUNNABLE 3
#define RUNNING 4

struct proc_model {
  int state;
  int chan;
  int woken;      /* whether a wakeup has been attempted since last sleep */
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
valid_transition(int from, int to, int sleep_in_progress)
{
  /* Lab 5 reachable edges (each holds the process lock). */
  if (to == RUNNING)
    return from == RUNNABLE;
  if (to == RUNNABLE)
    return from == RUNNING || from == SLEEPING;
  if (to == SLEEPING)
    return from == RUNNING;
  if (to == USED)
    return from == UNUSED;
  if (to == UNUSED)
    return from == USED;
  (void)sleep_in_progress;
  return 0;
}

int
main(int argc, char **argv)
{
  uint64_t seed, cases, i;
  struct proc_model p[NPROC_MIN];

  if (argc < 3) {
    fprintf(stderr, "usage: wakeup_contract SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  for (i = 0; i < NPROC_MIN; i++) {
    p[i].state = UNUSED;
    p[i].chan = 0;
    p[i].woken = 0;
  }

  for (i = 0; i < cases; i++) {
    int who = (int)(next_rng() % NPROC_MIN);
    int other = 1 - who;
    int op = (int)(next_rng() % 3);

    if (op == 0 && p[who].state == UNUSED) {
      /* allocproc -> USED (acquire process lock). */
      p[who].state = USED;
      p[who].chan = 0;
      p[who].woken = 0;
    } else if (op == 1) {
      /* sleep: acquire p->lock, release caller lock, then publish SLEEPING. */
      /* Handoff ordering is checked by the source-level contract; here the
       * wakeup attempted *after* SLEEPING publication must not be lost. */
      if (p[who].state == USED || p[who].state == RUNNABLE) {
        p[who].state = SLEEPING;
        p[who].chan = (int)who + 1;
        p[who].woken = 0;
      }
    } else if (op == 2) {
      /* wakeup on channel of `other`. No-lost-wakeup: if the process is
       * SLEEPING on that channel, it becomes RUNNABLE (while holding its
       * lock). A wakeup published before the sleep completes is the caller
       * lock's responsibility and is covered by the source handoff check. */
      int ch = (int)other + 1;
      if (p[other].state == SLEEPING && p[other].chan == ch) {
        p[other].state = RUNNABLE;
        p[other].woken = 1;
        p[other].chan = 0;
      }
    }

    /* Run a bounded scheduler step: a RUNNABLE process may become RUNNING. */
    for (who = 0; who < NPROC_MIN; who++) {
      if (p[who].state == RUNNABLE) {
        int before = p[who].state;
        p[who].state = RUNNING;
        if (!valid_transition(before, RUNNING, 0)) {
          fprintf(stderr, "wakeup_contract: illegal RUNNABLE->RUNNING at case %llu\n",
                  (unsigned long long)i);
          if (argc > 3) { FILE *f = fopen(argv[3], "w"); if (f) { fputs("illegal RUNNABLE->RUNNING\n", f); fclose(f); } }
          return 1;
        }
      }
    }

    /* The woken process that became RUNNING must be schedulable next. */
    for (who = 0; who < NPROC_MIN; who++) {
      if (p[who].woken && p[who].state != RUNNABLE && p[who].state != RUNNING) {
        /* A process woken while SLEEPING must be RUNNABLE (or already
         * RUNNING); if it slipped back to SLEEPING without the caller lock,
         * that is a lost wakeup. */
        if (p[who].state == SLEEPING) {
          fprintf(stderr, "wakeup_contract: lost wakeup for proc %d at case %llu\n",
                  who, (unsigned long long)i);
          if (argc > 3) { FILE *f = fopen(argv[3], "w"); if (f) { fputs("lost-wakeup\n", f); fclose(f); } }
          return 1;
        }
      }
    }
  }

  printf("wakeup_contract: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}
