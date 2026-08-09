#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel_boot banner fuzz harness (host toolchain).
 * Deterministic, fixed-seed, bounded-case fuzz of the banner contract:
 * - the literal "XV6_BOOT_OK\n" must always satisfy the public contract, and
 * - pseudo-random byte noise must never spuriously satisfy a single-banner
 *   occurrence oracle (so captured QEMU serial cannot be fooled by garbage).
 * Usage: banner_fuzz SEED CASES [REPRO]
 */
static const char EXPECTED[] = "XV6_BOOT_OK\n";

static uint64_t rng_state;

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

static int contract_holds(const char *b)
{
  const unsigned char *p;
  if (b == 0)
    return 0;
  for (p = (const unsigned char *)b; *p; ++p) {
    /* printable ASCII or a terminal-ish whitespace character only */
    if (!(*p >= 0x20 && *p <= 0x7e) &&
        !(*p == '\n' || *p == '\r' || *p == '\t'))
      return 0;
  }
  if (strstr(b, "XV6_BOOT_OK") == 0)
    return 0;
  return 1;
}

int main(int argc, char **argv)
{
  uint64_t seed, cases, i;
  if (argc < 3) {
    fprintf(stderr, "usage: banner_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  for (i = 0; i < cases; ++i) {
    int len = (int)(next_rng() % 63);
    char buf[80];
    int n = 0;
    int j;
    for (j = 0; j < len; ++j)
      buf[n++] = (char)(' ' + (next_rng() % 0x60));
    buf[n] = 0;

    if (strcmp(buf, EXPECTED) == 0) {
      /* Noise must never be able to short-circuit the single-banner oracle. */
      fprintf(stderr, "banner_fuzz: synthetic case equal to banner at case %llu\n",
              (unsigned long long)i);
      if (argc > 3) {
        FILE *f = fopen(argv[3], "w");
        if (f) { fputs(buf, f); fclose(f); }
      }
      return 1;
    }
    if (len > 0 && contract_holds(buf)) {
      /* A non-literal random printable run must not alone satisfy the count. */
      if (strstr(buf, "XV6_BOOT_OK") != 0) {
        fprintf(stderr, "banner_fuzz: unexpected partial banner at case %llu\n",
                (unsigned long long)i);
        if (argc > 3) {
          FILE *f = fopen(argv[3], "w");
          if (f) { fputs(buf, f); fclose(f); }
        }
        return 1;
      }
    }
  }

  if (!contract_holds(EXPECTED)) {
    fprintf(stderr, "banner_fuzz: literal failed the banner contract\n");
    if (argc > 3) {
      FILE *f = fopen(argv[3], "w");
      if (f) fputs(EXPECTED, f);
      if (f) fclose(f);
    }
    return 1;
  }

  printf("banner_fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}