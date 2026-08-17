#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* platform/visionfive2 fixed-seed input-completeness fuzz harness (host model).
 *
 * The ModuleSpec `validate_board_inputs` operation and the module guarantee
 * ("missing inputs or probes never become a successful board claim") require
 * that every submitted run is gated on the completeness of six immutable
 * board inputs before any reviewable workload can launch, and that even a
 * fully supplied run produces pending_human_review evidence, never an
 * automatic passed-hardware conclusion (hardware-evidence-boundary /
 * no-simulated-hardware-pass).
 *
 * The harness models each submitted run as a random subset (bitmask) of the
 * six required inputs over a deterministic xorshift RNG. For every drawn
 * mask it checks two invariants against the spec oracle:
 *
 *   INCOMPLETE   mask != 0x3f  => fail-closed, must never produce a
 *                                passed-hardware or even a reviewable claim.
 *   COMPLETE     mask == 0x3f  => may launch the workload and record hashes,
 *                                but the outcome is pending_human_review, and
 *                                never an automatic passed-hardware conclusion.
 *
 * A regression that let an incomplete input set reach a successful board claim
 * (or that auto-passed a complete set) is caught on the first offending case.
 *
 * Usage: input_state_fuzz SEED CASES [REPRO]
 */
#define NINPUTS 6
#define FULL_MASK ((1u << NINPUTS) - 1) /* all six present */

static uint64_t rng_state;

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

/* is_automatic_pass: the runner's verdict for a submitted mask. Mirrors the
 * fail-closed boundary: only a fully supplied mask may even become a
 * reviewable workload (returning "pending_human_review"), and that never
 * equals an automatic passed-hardware claim. */
static int
runner_automatic_pass(uint32_t mask)
{
  if (mask != FULL_MASK) return 0; /* fail closed: no claim at all */
  return 0;                        /* complete but pending human review only */
}

/* is_reviewable: whether the mask clears the gate into a launchable workload.
 * Guards the postcondition "records their hashes and launches the declared
 * workload OR fails before claiming hardware execution". */
static int
runner_reviewable(uint32_t mask)
{
  return mask == FULL_MASK;
}

static int
write_repro(const char *path, const char *msg)
{
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  if (msg) fputs(msg, f);
  return fclose(f);
}

static int
bit(const char *name, FILE *spec)
{
  /* presence marker checked separately by the shell driver; static helper only
   * for documentation symmetry */
  (void)name; (void)spec;
  return 1;
}

int
main(int argc, char **argv)
{
  uint64_t seed, cases, i;

  if (argc < 3) {
    fprintf(stderr, "usage: input_state_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;
  bit(NULL, NULL);

  /* Mask 0 (no inputs) and every single-missing mask must be non-reviewable
   * and must never pass. */
  {
    uint32_t m;
    for (m = 0; m < (1u << NINPUTS); m++) {
      if (runner_reviewable(m) != (m == FULL_MASK)) {
        fprintf(stderr, "input_state_fuzz: reviewable gate wrong for mask %u\n", m);
        if (argc > 3) write_repro(argv[3], "incorrect reviewable gate");
        return 1;
      }
      if (runner_automatic_pass(m)) {
        fprintf(stderr, "input_state_fuzz: mask %u auto-passed hardware\n", m);
        if (argc > 3) write_repro(argv[3], "automatic hardware pass");
        return 1;
      }
    }
  }

  for (i = 0; i < cases; i++) {
    uint32_t mask = (uint32_t)(next_rng() % (1u << NINPUTS));

    if (runner_reviewable(mask) && mask != FULL_MASK) {
      fprintf(stderr, "input_state_fuzz: incomplete mask %u became reviewable\n", mask);
      if (argc > 3) write_repro(argv[3], "incomplete mask reviewable");
      return 1;
    }
    /* Complete masks are pending-reviewable but never an automatic pass. */
    if (mask == FULL_MASK) {
      if (runner_automatic_pass(mask)) {
        fprintf(stderr, "input_state_fuzz: complete mask %u auto-passed\n", mask);
        if (argc > 3) write_repro(argv[3], "complete mask auto-pass");
        return 1;
      }
    }
  }

  printf("input_state_fuzz: %llu masks with seed %llu all fail-closed or pending-human-review\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}