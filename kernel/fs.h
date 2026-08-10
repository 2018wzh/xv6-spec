// fs.h - the xv6 on-disk file-system layout and inode structures owned by
// kernel/inode.
//
// kernel/inode owns the xv6 on-disk layout, deterministic image construction
// (mkfs), block and inode allocation, the inode cache, directories, and
// component-wise path traversal. This header declares the superblock,
// on-disk inode (dinode) and directory entry types, the in-memory inode
// structure, and the inode-module operation surface bound by the module
// Spec.
//
// Layout (1024-byte logical blocks; each logical block maps to two adjacent
// 512-byte virtio sectors):
//   block 0            : unused (reserved)
//   block 1            : superblock (struct superblock)
//   blocks 2..1+nlog   : log region (redo log, owned by kernel/log)
//   blocks inodestart.. : inode region (ninodes dinodes)
//   blocks bmapstart..  : allocation bitmap
//   blocks data          : file data
//   total size          : FSSIZE blocks (param.h)
//
// Functions that read or write on-disk metadata run only inside a log
// transaction (begin_op/end_op) and through the buffer cache, so committed
// metadata mutations are atomic and survive a crash via kernel/log recovery.

#ifndef __FS_H__
#define __FS_H__

#include "types.h"
#include "param.h"

// Inode/metadata I/O status struct returned by stati(). This is the inode-
// module-owned stat shape; kernel/file's fstat syscall aggregates it through
// the file-ABI layer. Device and inode are the owning file-system identity,
// and nlink is the persistent directory-reference count.
struct stat {
  int dev;      // File system's device number.
  uint ino;     // Inode number.
  short type;   // Type of file (T_DIR/T_FILE/T_DEVICE).
  short nlink;  // Number of hard links.
  uint64 size;  // Size of file in bytes.
};

// On-disk file system magic number (matches the redo-log superblock subset).
#define FSMAGIC 0x10203040

// Number of direct block addresses in an inode's block list.
#define NDIRECT 12
// Number of block addresses in a single-indirect block (a full data block).
#define NINDIRECT (BSIZE / sizeof(uint))
// Maximum file size in blocks (direct + single indirect).
#define MAXFILE (NDIRECT + NINDIRECT)

// The on-disk superblock, validated by fsinit before any mutable mount.
struct superblock {
  uint magic;       // Must be FSMAGIC.
  uint size;        // Size of file system image (blocks).
  uint nblocks;     // Number of data blocks.
  uint ninodes;     // Number of inodes.
  uint nlog;        // Number of log blocks.
  uint logstart;    // Block number of first log block.
  uint inodestart;  // Block number of first inode block.
  uint bmapstart;   // Block number of first free map block.
};

// On-disk inode (dinode), stored compactly in the inode region. The layout
// block addresses use direct (NDIRECT) pointers plus a single indirect block.
struct dinode {
  short type;               // File type: FILE, DIR, or DEVICE.
  short major;              // Major device number (DEVICE only).
  short minor;              // Minor device number (DEVICE only).
  short nlink;              // Number of hard links (directory references).
  uint size;                // Size of file (bytes).
  uint addrs[NDIRECT + 1];  // Data block addresses (last is indirect block).
};

// Inode modes (file types); also used by stat.
#define T_DIR     1   // Directory
#define T_FILE    2   // Regular file
#define T_DEVICE  3   // Device

// On-disk directory entry. A directory is a sequence of dirents; a zero
// inum entry is "free".
#define DIRSIZ 14

struct dirent {
  ushort inum;            // Inode number.
  char name[DIRSIZ];      // File name (NUL-padded, not necessarily NUL-terminated).
};

// In-memory inode cache entry. Each live (dev, inum) pair has one cached
// identity with a nonnegative reference count; link counts plus open
// references govern final reclamation.
struct inode {
  uint dev;               // Device number of the file system.
  uint inum;              // Inode number.
  int ref;                // Reference count (inode-cache-identity).
  struct sleeplock lock;  // Protects everything below (per-inode sleep lock).
  int valid;              // Are the fields below valid (loaded from disk)?

  // ---- fields below are copied / updated from the on-disk dinode ----
  short type;             // Copy of disk inode (type).
  short major;            // Copy of disk inode (major).
  short minor;            // Copy of disk inode (minor).
  short nlink;            // Copy of disk inode (nlink).
  uint size;              // Copy of disk inode (size).
  uint addrs[NDIRECT + 1]; // Copy of disk inode (addrs).
};

// kernel/inode owned operation surface.
void             fsinit(int dev);
int              readi(struct inode *, int, uint64, uint, uint);
int              writei(struct inode *, int, uint64, uint, uint);
void             stati(struct inode *, struct stat *);
struct inode*    iget(uint dev, uint inum);
struct inode*    idup(struct inode *);
void             iput(struct inode *);
void             iunlock(struct inode *);
void             iunlockput(struct inode *);
void             ilock(struct inode *);
void             iupdate(struct inode *);
void             iinit(void);
struct inode*    dirlookup(struct inode *, const char *, uint *);
int              dirlink(struct inode *, const char *, uint);
int              unlink_helper(const char *, struct inode **, struct inode **);
struct inode*    namei(const char *);
struct inode*    nameiparent(const char *, char *);
int              balloc_alloc(uint dev);
void             bfree_release(uint dev, uint b);
uint             bmap(struct inode *, uint);
int              itrunc(struct inode *);
struct inode*    ialloc_alloc(uint dev, short type);
struct inode*    dnamex(const char *, int, char *);

#endif // __FS_H__