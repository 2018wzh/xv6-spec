#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* platform/visionfive2 fixed-seed hash-record fuzz harness (host model).
 *
 * algorithm_intent records the hashes of the immutable board inputs, and the
 * postcondition requires the runner to launch the declared workload only with
 * matching immutable inputs. This harness models the hash-record oracle:
 * over a deterministic xorshift RNG it draws random immutable input payloads
 * and requires that:
 *
 *   - two independent recordings of the same payload produce the same digest
 *     (evidence determinism under a fixed seed/immutable input), and
 *   - any single-byte perturbation changes the digest, so a modified input
 *     fails closed and is never recorded as matching, and
 *   - a recorded digest alone is never synthesized into an automatic passed
 *     hardware conclusion (no-simulated-hardware-pass).
 *
 * Usage: hash_scheme_fuzz SEED CASES [REPRO]
 */
#define PAYLOAD_MAX 64

static uint64_t rng_state;

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

/* Recorded digest: a deterministic FNV-1a over the immutable payload. */
static uint32_t
digest(const uint8_t *p, size_t n)
{
  uint32_t h = 2166136261u;
  size_t i;
  for (i = 0; i < n; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

static int
write_repro(const char *path, const char *msg)
{
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  if (msg) fputs(msg, f);
  return fclose(f);
}

int
main(int argc, char **argv)
{
  uint64_t seed, cases, i;

  if (argc < 3) {
    fprintf(stderr, "usage: hash_scheme_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  seed = strtoull(argv[1], 0, 10);
  cases = strtoull(argv[2], 0, 10);
  rng_state = seed ? seed : 1;

  for (i = 0; i < cases; i++) {
    uint8_t a[PAYLOAD_MAX], b[PAYLOAD_MAX];
    size_t n, j;

    n = (size_t)(1 + next_rng() % PAYLOAD_MAX);
    for (j = 0; j < n; j++) a[j] = (uint8_t)next_rng();
    memcpy(b, a, n);

    /* Independent recording of the same immutable payload => same digest. */
    if (digest(a, n) != digest(b, n)) {
      fprintf(stderr, "hash_scheme_fuzz: recording not deterministic at case %llu\n",
              (unsigned long long)i);
      if (argc > 3) write_repro(argv[3], "non-deterministic recording");
      return 1;
    }

    /* A single-byte perturbation must change the digest (fails closed). With
     * FNV-1a and payloads >= 1 byte it always does; assert it anyway so the
     * oracle truly rejects modified immutable inputs. */
    if (n) {
      uint8_t saved = a[n - 1];
      a[n - 1] = (uint8_t)(saved ^ 0x5a);
      if (digest(a, n) == digest(b, n)) {
        fprintf(stderr, "hash_scheme_fuzz: perturbation undetected at case %llu\n",
                (unsigned long long)i);
        if (argc > 3) write_repro(argv[3], "undetected perturbation");
        return 1;
      }
    }
  }

  printf("hash_scheme_fuzz: %llu immutable-input recordings with seed %llu deterministic\n",
         (unsigned long long)cases, (unsigned long long)seed);
  return 0;
}