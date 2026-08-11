#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel_boot bounded fixed-seed fuzz of the single-line banner property.
 *
 * Links against the real kernel/boot.c via boot_banner(). The kernel_boot
 * single-banner occurrence oracle must never be fooled: so for a fixed seed
 * and bounded number of cases this harness verifies
 *   - the banner is bounded in size and safe to read at any byte offset,
 *   - the banner is exactly a single logical line (exactly one trailing
 *     newline, no carriage return),
 *   - the banner contains exactly one literal "XV6_BOOT_OK" occurrence.
 * A two-line or duplicated banner would break the deterministic single-banner
 * runtime oracle, so this must never hold for the committed implementation.
 * Usage: banner_single_line_fuzz SEED CASES [REPRO]
 */

extern const char *boot_banner(void);

static uint64_t rng_state;

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

static int write_repro(const char *path, const char *data)
{
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;
  if (data)
    fputs(data, f);
  return fclose(f);
}

int main(int argc, char **argv)
{
  uint64_t seed, cases, i;
  const char *b;
  size_t blen, j;
  const char *first, *second;

  if (argc < 3) {
    fprintf(stderr, "usage: banner_single_line_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  b = boot_banner();
  if (!b) {
    fprintf(stderr, "banner_single_line_fuzz: null banner\n");
    if (argc > 3) write_repro(argv[3], "null");
    return 1;
  }
  blen = strlen(b);
  if (blen == 0 || blen > 128) {
    fprintf(stderr, "banner_single_line_fuzz: banner size %zu out of range\n", blen);
    if (argc > 3) write_repro(argv[3], b);
    return 1;
  }

  /* Must be a single logical line: exactly one terminating newline, no CR. */
  {
    size_t nls = 0;
    for (j = 0; j < blen; ++j) {
      if (b[j] == '\r') {
        fprintf(stderr, "banner_single_line_fuzz: banner contains CR\n");
        if (argc > 3) write_repro(argv[3], b);
        return 1;
      }
      if (b[j] == '\n')
        ++nls;
    }
    if (nls != 1) {
      fprintf(stderr, "banner_single_line_fuzz: newline count %zu != 1\n", nls);
      if (argc > 3) write_repro(argv[3], b);
      return 1;
    }
  }

  /* Exactly one literal occurrence of the marker. */
  first = strstr(b, "XV6_BOOT_OK");
  if (!first) {
    fprintf(stderr, "banner_single_line_fuzz: banner lacks XV6_BOOT_OK\n");
    if (argc > 3) write_repro(argv[3], b);
    return 1;
  }
  second = strstr(first + 1, "XV6_BOOT_OK");
  if (second) {
    fprintf(stderr, "banner_single_line_fuzz: duplicate XV6_BOOT_OK\n");
    if (argc > 3) write_repro(argv[3], b);
    return 1;
  }

  /* Random safe-access interleavings: reading any banner byte is legal and
   * the pointer stays stable across the whole run (immutability). */
  for (i = 0; i < cases; ++i) {
    size_t r = (size_t)(next_rng() % blen);
    (void)b[r];
    if (boot_banner() != b) {
      fprintf(stderr, "banner_single_line_fuzz: pointer drifted at case %llu\n",
              (unsigned long long)i);
      if (argc > 3) write_repro(argv[3], b);
      return 1;
    }
  }

  printf("banner_single_line_fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}
