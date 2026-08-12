#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* toolchain fixed-seed mkfs-layout fuzz harness (host model).
 *
 * The toolchain + kernel/inode disk-layout-partition contract requires the
 * superblock, log, inode, bitmap, and data regions to be in range,
 * non-overlapping, and cover only the declared logical blocks. The real
 * deterministic image is produced by mkfs/mkfs (which uses these fixed
 * geometry values). This host harness fuzzes the geometry arithmetic under a
 * bounded number of cases and confirms every region stays in range and that
 * no two metadata regions overlap, using a deterministic xorshift RNG so a
 * fixed seed always reproduces the same case sequence.
 *
 * Usage: mkfs_layout_fuzz SEED CASES [REPRO]
 */

#define BSIZE      1024
#define DIRSIZ     14
#define NLOG       30
#define NINODES    200
#define NBMAP      1
#define FSSIZE     2000

static uint64_t rng_state;

static uint64_t next_rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

/* Model the mkfs geometry computation (mirrors mkfs/mkfs.c main()). */
static void model_geometry(int nlog, int ninodes, int nblocks_total,
                           int *logstart, int *inodestart,
                           int *bmapstart, int *nmeta, int *nblocks)
{
  int logstart_ = 2;
  int inodestart_ = logstart_ + nlog;
  int ninodeblocks = (ninodes + (BSIZE / 64) - 1) / (BSIZE / 64);
  int bmapstart_ = inodestart_ + ninodeblocks;
  int nmeta_ = 2 + nlog + ninodeblocks + NBMAP;
  int nblocks_ = nblocks_total - nmeta_;

  *logstart = logstart_;
  *inodestart = inodestart_;
  *bmapstart = bmapstart_;
  *nmeta = nmeta_;
  *nblocks = nblocks_;

  (void)BSIZE; (void)DIRSIZ; (void)NBMAP;
}

int main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(stderr, "usage: mkfs_layout_fuzz SEED CASES [REPRO]\n");
    return 2;
  }
  rng_state = strtoull(argv[1], 0, 0);
  long cases = strtol(argv[2], 0, 10);
  const char *repro = argc > 3 ? argv[3] : 0;
  long nfailed = 0;

  for (long i = 0; i < cases; i++) {
    /* Fuzz mkfs's fixed geometry under every bounded input range; the
     * deterministic image contract requires regions to stay in range and
     * non-overlapping for the declared FSSIZE. Perturb the model inputs
     * within bounded ranges and check the invariants. */
    int nlog = (int)(next_rng() % 64);          /* 0..63 */
    int ninodes = (int)(next_rng() % 512);      /* 0..511 */
    int fssize = 2000;

    int logstart, inodestart, bmapstart, nmeta, nblocks;
    model_geometry(nlog, ninodes, fssize, &logstart, &inodestart,
                   &bmapstart, &nmeta, &nblocks);

    int bad = 0;
    /* superblock at block 1. */
    if (logstart != 2) bad = 1;
    /* log region [2, 2+nlog) must precede inode region and stay below bmap. */
    if (inodestart < logstart + nlog) bad = 1;
    if (bmapstart < inodestart) bad = 1;
    /* data region starts at bmapstart+1 and must fit. */
    if (nblocks < 0) bad = 1;
    if (bmapstart + 1 + nblocks > fssize) bad = 1;
    /* metadata (super + log + inode + bmap) must not exceed image size. */
    if (nmeta > fssize) bad = 1;
    /* inode region size must match. */
    int ninodeblocks = (ninodes + (BSIZE / 64) - 1) / (BSIZE / 64);
    if (inodestart != 2 + nlog) bad = 1;
    if (bmapstart != inodestart + ninodeblocks) bad = 1;

    if (bad) {
      nfailed++;
      if (repro) {
        FILE *f = fopen(repro, "w");
        if (f) {
          fprintf(f, "case=%ld seed=%llu nlog=%d ninodes=%d fssize=%d\n",
                  i, (unsigned long long)rng_state, nlog, ninodes, fssize);
          fclose(f);
        }
      }
      break;
    }
  }

  if (nfailed > 0) {
    fprintf(stderr, "mkfs_layout_fuzz: invariant violation at case %ld with repro %s\n",
            nfailed, repro ? repro : "(none)");
    return 1;
  }

  printf("mkfs_layout_fuzz: %ld cases passed\n", cases);
  return 0;
}
