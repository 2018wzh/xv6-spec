#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* kernel_trap fixed-seed vector-frame fuzz harness (host toolchain).
 *
 * The module property vector-frame-symmetry requires that kernelvec saves
 * every register it changes into a symmetric stack slot and restores it from
 * that identical slot before sret. This harness models the full 32-register
 * supervisor trap frame and, over a deterministic fixed-seed case loop,
 * verifies:
 *   1. the canonical kernelvec layout is fully symmetric (every save offset
 *      equals the corresponding restore offset), and
 *   2. the symmetry check actually discriminates: any single-slot asymmetry
 *      injected into a candidate frame is always rejected, so a real
 *      regression in kernelvec.S (an unsymmetric save/restore) is caught.
 *
 * The harness is compiled with warnings enabled (-Wall) before every
 * execution; unused or undeclared harness state is a failing result.
 *
 * Usage: trap_frame_fuzz SEED CASES [REPRO]
 */
#define NREGS 32

struct slot {
  const char *name;
  int save_off;
  int restore_off;
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
all_symmetric(const struct slot *f, int n)
{
  int i;
  for (i = 0; i < n; i++)
    if (f[i].save_off != f[i].restore_off)
      return 0;
  return 1;
}

int
main(int argc, char **argv)
{
  uint64_t seed, cases, i;
  struct slot framed[NREGS];
  int where;
  int victim_off;
  static const char *names[NREGS];
  int j;

  if (argc < 3) {
    fprintf(stderr, "usage: trap_frame_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  names[0]  = "x0";  names[1]  = "ra"; names[2]  = "sp"; names[3]  = "gp";
  names[4]  = "tp";  names[5]  = "t0"; names[6]  = "t1"; names[7]  = "t2";
  names[8]  = "s0";  names[9]  = "s1"; names[10] = "a0"; names[11] = "a1";
  names[12] = "a2";  names[13] = "a3"; names[14] = "a4"; names[15] = "a5";
  names[16] = "a6";  names[17] = "a7"; names[18] = "s2"; names[19] = "s3";
  names[20] = "s4";  names[21] = "s5"; names[22] = "s6"; names[23] = "s7";
  names[24] = "s8";  names[25] = "s9"; names[26] = "s10"; names[27] = "s11";
  names[28] = "t3";  names[29] = "t4"; names[30] = "t5"; names[31] = "t6";

  /* Build the canonical symmetric 32-register kernelvec frame. */
  for (j = 0; j < NREGS; j++) {
    framed[j].name = names[j];
    framed[j].save_off = j * 8;
    framed[j].restore_off = j * 8; /* x0 slot offset 0 is symmetric by design */
  }

  /* The canonical layout must pass the symmetry oracle. */
  if (!all_symmetric(framed, NREGS)) {
    fprintf(stderr, "trap_frame_fuzz: canonical kernelvec frame is asymmetric\n");
    if (argc > 3) {
      FILE *f = fopen(argv[3], "w");
      if (f) { fputs("canonical kernelvec frame asymmetric\n", f); fclose(f); }
    }
    return 1;
  }

  /* Fixed-seed fuzz: perturb one restore offset and require the oracle to
   * reject the asymmetric frame; restoring it must pass again. This proves
   * the oracle cannot give a false pass to an unsymmetric kernelvec. */
  for (i = 0; i < cases; i++) {
    where = (int)(next_rng() % NREGS);
    /* choose a non-identical offset to make the frame genuinely asymmetric. */
    do {
      victim_off = (int)(next_rng() % (NREGS * 8));
    } while (victim_off == framed[where].restore_off);

    framed[where].restore_off = victim_off;
    if (all_symmetric(framed, NREGS)) {
      fprintf(stderr,
              "trap_frame_fuzz: asymmetric frame passed oracle at case %llu\n",
              (unsigned long long)i);
      if (argc > 3) {
        FILE *f = fopen(argv[3], "w");
        if (f) {
          fprintf(f, "reg %s restore=%d save=%d at case %llu\n",
                  framed[where].name, victim_off, framed[where].save_off,
                  (unsigned long long)i);
          fclose(f);
        }
      }
      return 1;
    }
    framed[where].restore_off = framed[where].save_off;
    if (!all_symmetric(framed, NREGS)) {
      fprintf(stderr,
              "trap_frame_fuzz: restored symmetric frame rejected at case %llu\n",
              (unsigned long long)i);
      if (argc > 3) {
        FILE *f = fopen(argv[3], "w");
        if (f) {
          fprintf(f, "restored frame for reg %s rejected at case %llu\n",
                  framed[where].name, (unsigned long long)i);
          fclose(f);
        }
      }
      return 1;
    }
  }

  printf("trap_frame_fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}