// mkfs.c - deterministic root file-system image generator (kernel/inode).
//
// This host tool builds the fs.img image that the kernel mounts via fsinit.
// It emits the same 1024-byte logical block layout the kernel consumes, so
// the superblock, log region, inode region, bitmap, and data region are
// mutually consistent and within range (disk-layout-partition):
//
//   block 0            : reserved (zeroed)
//   block 1            : superblock (struct superblock)
//   blocks 2 .. 1+nlog : log region (redo log, kernel/log)
//   blocks inodestart.. : inode region (ninodes dinodes)
//   block  bmapstart    : allocation bitmap
//   blocks bmapstart+1..: data region
//
// Each data block is represented by exactly one bit in the bitmap; the whole
// image is written deterministically so repeated runs reproduce the same
// fs.img. The kernel's fsinit and initlog validate this geometry before any
// mutable mount (disk-layout-partition).

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Mirror the kernel on-disk layout from kernel/fs.h. Deliberately re-declared
 * (not #included) so this host tool does not pull in kernel types.h/param.h
 * that conflict with the host toolchain. Field order and sizes match the
 * kernel structs exactly. */
#define BSIZE     1024
#define NDIRECT   12
#define DIRSIZ    14
#define FSMAGIC   0x10203040
#define T_DIR     1
#define T_FILE    2
#define T_DEVICE  3
#define IPB       (BSIZE / sizeof(struct dinode))

#define FSSIZE      2000
#define NLOG        30
#define NINODES     200
#define NBMAPBLOCKS 1
#define ROOTINO     1

struct superblock {
  uint32_t magic;
  uint32_t size;
  uint32_t nblocks;
  uint32_t ninodes;
  uint32_t nlog;
  uint32_t logstart;
  uint32_t inodestart;
  uint32_t bmapstart;
};

struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint32_t size;
  uint32_t addrs[NDIRECT + 1];
};

struct dirent {
  uint16_t inum;
  char name[DIRSIZ];
};

/* ---- geometry (computed once) ---- */
static int logstart, inodestart, bmapstart, ninodeblocks, nmeta, nblocks;

/* The whole in-memory image (g_imgblocks logical blocks). */
static unsigned char *g_img;
static struct dinode *g_inodes;   /* logical inode array (size NINODES) */
static unsigned char g_bbuf[BSIZE]; /* bitmap block */

static void
fatal(const char *msg)
{
  fprintf(stderr, "mkfs: %s\n", msg);
  exit(1);
}

static unsigned char *
img_block(uint32_t blk)
{
  return g_img + (uintptr_t)blk * BSIZE;
}

static struct dinode *
get_inode(int inum)
{
  return &g_inodes[inum];
}

/* Allocate one data block from the bitmap. Returns its absolute block number
 * (>= bmapstart+1) or 0 when the data region is full. */
static uint32_t
balloc(void)
{
  int b;
  for (b = 0; b < nblocks; b++) {
    int byte = b / 8;
    int bit = b % 8;
    if ((g_bbuf[byte] & (1 << bit)) == 0) {
      g_bbuf[byte] |= (1 << bit);
      return (uint32_t)(bmapstart + 1 + b);
    }
  }
  return 0;
}

/* Append n bytes at *p to logical file `inum`. Extends the inode block list
 * (direct then single-indirect) exactly as the kernel's bmap does, so the
 * generated image is directly consumable by kernel readi/bmap. */
static void
iappend(uint32_t inum, const void *xp, uint32_t n)
{
  struct dinode *ip = get_inode(inum);
  uint32_t off = ip->size;
  uint32_t total = n;
  const unsigned char *p = (const unsigned char *)xp;

  while (total > 0) {
    uint32_t blockidx = off / BSIZE;   /* logical block index within file */
    uint32_t offset = off % BSIZE;
    uint32_t chunk = total;
    if (chunk > BSIZE - offset)
      chunk = BSIZE - offset;

    uint32_t addr;
    if (blockidx < NDIRECT) {
      if (ip->addrs[blockidx] == 0) {
        addr = balloc();
        if (addr == 0)
          fatal("out of direct data blocks");
        ip->addrs[blockidx] = addr;
      }
      addr = ip->addrs[blockidx];
    } else {
      /* single-indirect: blockidx - NDIRECT indexes into the indirect block. */
      uint32_t idx = blockidx - NDIRECT;
      if (ip->addrs[NDIRECT] == 0) {
        uint32_t ind = balloc();
        if (ind == 0)
          fatal("out of indirect block");
        ip->addrs[NDIRECT] = ind;
        memset(img_block(ind), 0, BSIZE);
      }
      {
        uint32_t *a = (uint32_t *)img_block(ip->addrs[NDIRECT]);
        if (a[idx] == 0) {
          uint32_t d = balloc();
          if (d == 0)
            fatal("out of indirect data blocks");
          a[idx] = d;
        }
        addr = a[idx];
      }
    }

    memcpy(img_block(addr) + offset, p, chunk);
    p += chunk;
    off += chunk;
    total -= chunk;
  }
  ip->size = off;
}

/* Write a directory entry into a directory inode. */
static void
dir_entry(uint32_t dirmum, const char *name, uint32_t inum)
{
  struct dirent de;
  uint32_t l;
  memset(&de, 0, sizeof(de));
  de.inum = (uint16_t)inum;
  l = (uint32_t)strlen(name);
  if (l >= DIRSIZ)
    l = DIRSIZ - 1;
  memcpy(de.name, name, l);
  de.name[l] = 0;
  iappend(dirmum, &de, sizeof(de));
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if (argc != 2)
    fatal("usage: mkfs fs.img");

  /* ---- geometry ---- */
  logstart = 2;
  inodestart = logstart + NLOG;
  ninodeblocks = (NINODES + IPB - 1) / IPB;
  bmapstart = inodestart + ninodeblocks;
  nmeta = 2 + NLOG + ninodeblocks + NBMAPBLOCKS;
  nblocks = FSSIZE - nmeta;

  g_img = calloc(FSSIZE, BSIZE);
  g_inodes = calloc(NINODES, sizeof(struct dinode));
  if (g_img == 0 || g_inodes == 0)
    fatal("out of memory");
  memset(g_bbuf, 0, sizeof(g_bbuf));

  /* ---- superblock at block 1 ---- */
  {
    struct superblock sb;
    sb.magic = FSMAGIC;
    sb.size = FSSIZE;
    sb.nblocks = nblocks;
    sb.ninodes = NINODES;
    sb.nlog = NLOG;
    sb.logstart = logstart;
    sb.inodestart = inodestart;
    sb.bmapstart = bmapstart;
    memcpy(img_block(1), &sb, sizeof(sb));
  }

  /* ---- bitmap: mark all metadata blocks 0..nmeta-1 allocated ---- */
  for (i = 0; i < nmeta; i++) {
    int byte = i / 8;
    int bit = i % 8;
    if (byte < BSIZE)
      g_bbuf[byte] |= (1 << bit);
  }

  /* ---- root inode (inum 1) ---- */
  {
    struct dinode *root = get_inode(ROOTINO);
    root->type = T_DIR;
    root->nlink = 3;   /* ".", ".." (from child), and the mount link */
    root->size = 0;
  }
  dir_entry(ROOTINO, ".", ROOTINO);
  dir_entry(ROOTINO, "..", ROOTINO);

  /* ---- /README (inum 2), a bounded regular file ---- */
  {
    struct dinode *f = get_inode(2);
    const char *content = "kernel/inode deterministic root image\n";
    f->type = T_FILE;
    f->nlink = 1;
    f->size = 0;
    iappend(2, content, (uint32_t)strlen(content));
  }
  dir_entry(ROOTINO, "README", 2);

  /* ---- /test (inum 3), a subdirectory ---- */
  {
    struct dinode *d = get_inode(3);
    d->type = T_DIR;
    d->nlink = 2;
    d->size = 0;
  }
  dir_entry(ROOTINO, "test", 3);
  dir_entry(3, ".", 3);
  dir_entry(3, "..", ROOTINO);

  /* ---- /test/t1 (inum 4), a regular file ---- */
  {
    struct dinode *f = get_inode(4);
    const char *content = "t1 hello\n";
    f->type = T_FILE;
    f->nlink = 1;
    f->size = 0;
    iappend(4, content, (uint32_t)strlen(content));
  }
  dir_entry(3, "t1", 4);

  /* ---- write the inode region ---- */
  for (i = 0; i < NINODES; i++) {
    unsigned char *dst = img_block(inodestart + i / IPB) + (i % IPB) * sizeof(struct dinode);
    memcpy(dst, &g_inodes[i], sizeof(struct dinode));
  }

  /* ---- write the bitmap block ---- */
  memcpy(img_block(bmapstart), g_bbuf, sizeof(g_bbuf));

  /* ---- emit fs.img ---- */
  fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    fatal("cannot create image");
  if (write(fd, g_img, (size_t)FSSIZE * BSIZE) != (ssize_t)((size_t)FSSIZE * BSIZE))
    fatal("short write to image");

  printf("mkfs: wrote %s (%d blocks)\n", argv[1], FSSIZE);
  close(fd);
  return 0;
}