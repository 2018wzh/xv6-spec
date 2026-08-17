#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel_boot immutable-banner fuzz harness (host toolchain).
 *
 * The module contract (kernel/boot.boot_banner) states that the boot banner
 * is an immutable printable string containing XV6_BOOT_OK. This harness
 * links against the real kernel/boot.c via boot_banner() and, for a fixed
 * seed and bounded number of cases, verifies:
 *   - boot_banner() is stable: repeated calls return the same pointer and
 *     the same content (immutability),
 *   - the banner is printable ASCII / terminal whitespace only,
 *   - the banner contains the literal "XV6_BOOT_OK",
 *   - a single-byte mutation of the banner can never equal the canonical
 *     banner, so a single-banner occurrence oracle cannot be fooled by a
 *     one-byte perturbed banner.
 * Usage: banner_immutable_fuzz SEED CASES [REPRO]
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

static int is_printable(const unsigned char *b)
{
  const unsigned char *p;
  for (p = b; *p; ++p) {
    if (!(*p >= 0x20 && *p <= 0x7e) &&
        !(*p == '\n' || *p == '\r' || *p == '\t'))
      return 0;
  }
  return 1;
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
  const char *a, *banner;
  char canonical[128];
  char mutated[128];
  size_t blen;

  if (argc < 3) {
    fprintf(stderr, "usage: banner_immutable_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  /* Capture the canonical banner once for stability and content checks. */
  a = boot_banner();
  if (a == 0) {
    fprintf(stderr, "banner_immutable_fuzz: boot_banner returned NULL\n");
    if (argc > 3) write_repro(argv[3], "null");
    return 1;
  }
  blen = strlen(a);
  if (blen == 0 || blen >= sizeof(canonical)) {
    fprintf(stderr, "banner_immutable_fuzz: banner length out of range\n");
    if (argc > 3) write_repro(argv[3], a);
    return 1;
  }
  memcpy(canonical, a, blen + 1);

  if (!is_printable((const unsigned char *)a)) {
    fprintf(stderr, "banner_immutable_fuzz: banner is not printable\n");
    if (argc > 3) write_repro(argv[3], a);
    return 1;
  }
  if (strstr(a, "XV6_BOOT_OK") == 0) {
    fprintf(stderr, "banner_immutable_fuzz: banner lacks XV6_BOOT_OK\n");
    if (argc > 3) write_repro(argv[3], a);
    return 1;
  }

  /* Per-case checks. */
  for (i = 0; i < cases; ++i) {
    banner = boot_banner();
    /* Immutability: same pointer and same content on every call. */
    if (banner != a) {
      fprintf(stderr, "banner_immutable_fuzz: pointer unstable at case %llu\n",
              (unsigned long long)i);
      if (argc > 3) write_repro(argv[3], a);
      return 1;
    }
    if (memcmp(banner, canonical, blen + 1) != 0) {
      fprintf(stderr, "banner_immutable_fuzz: content drifted at case %llu\n",
              (unsigned long long)i);
      if (argc > 3) write_repro(argv[3], a);
      return 1;
    }

    /* Single-byte mutation must not equal the canonical banner, so the
     * single-banner occurrence oracle sees exactly one canonical banner.
     * Pick a replacement byte guaranteed to differ from the original so a
     * coincidental identical byte cannot masquerade as a no-op. */
    {
      size_t at = (size_t)(next_rng() % blen);
      int alt;
      do {
        alt = ' ' + (int)(next_rng() % 0x60);
      } while (alt == (unsigned char)canonical[at]);
      memcpy(mutated, canonical, blen + 1);
      mutated[at] = (char)alt;
    }
    if (memcmp(mutated, canonical, blen + 1) == 0) {
      fprintf(stderr, "banner_immutable_fuzz: no-op mutation at case %llu\n",
              (unsigned long long)i);
      if (argc > 3) write_repro(argv[3], a);
      return 1;
    }
  }

  printf("banner_immutable_fuzz: %llu cases with seed %llu passed\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}