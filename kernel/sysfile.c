// sysfile.c - the Lab 6 validated user file syscalls (kernel/file).
//
// kernel/file aggregates the inode primitives (path_resolution with dirlookup/
// dirlink/unlink_helper, ialloc_alloc, readi/writei/stati) and the global
// file-table operations into the validated user file syscall surface:
// open/read/write/close/fstat/dup and the namespace mutations
// mkdir/mknod/chdir/unlink/link. Every user path and buffer crosses only
// through the validated copy helpers (argstr/copyin/copyout); C code never
// dereferences a raw user virtual address.
//
// Each success publishes exactly one initialized global file reference in one
// process descriptor slot. A descriptor is published only after its global
// file and inode references are initialized. All error paths balance file,
// descriptor, inode, user-buffer, and transaction references.

#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "buf.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "defs.h"

// Allocate a process descriptor slot and publish the given global file
// reference in it. Returns the descriptor index, or -1 when the per-process
// descriptor table is full (file_capacity) without leaking the reference.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;   // publish exactly one reference per populated slot.
      return fd;
    }
  }
  return -1;
}

// Create a new inode at the parent directory of `path`, or return the
// existing inode when it already exists without creating. This is the inode-
// creation primitive aggregated by sys_open(O_CREATE), sys_mkdir, and
// sys_mknod. Returns a referenced locked inode (caller releases) or 0.
static struct inode *
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;   // an existing regular file is reused by open(O_CREATE).
    if (type == T_DIR && ip->type == T_DIR)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc_alloc(dp->dev, type)) == 0) {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) {
    // Create . and .. entries for a new directory.
    dp->nlink++;  // .. refers to the parent.
    iupdate(dp);
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  iunlockput(dp);
  return ip;   // referenced locked inode; caller commonly unlocks+puts.

fail:
  if (ip->type == T_DIR) {
    dp->nlink--;  // undo the parent link otherwise leaked.
    iupdate(dp);
  }
  ip->nlink = 0;
  iupdate(ip);
  iput(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

// sys_open: open or create a file and return a process descriptor referencing
// one live global file object; -1 on any validation failure without a leak.
uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;
  if (argint(1, &omode) < 0)
    return -1;

  begin_op();
  if (omode & ~(O_RDONLY | O_WRONLY | O_RDWR | O_CREATE | O_TRUNC)) {
    end_op();
    return -1;   // unsupported mode bits: validation failure.
  }

  if (omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
  } else {
    ip = namei(path);
    if (ip == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
  }
  if (ip == 0) {
    end_op();
    return -1;
  }

  if (ip->type == T_DIR && omode != O_RDONLY) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      fileclose(f);   // fdalloc failed: never leak the global file reference.
    iunlockput(ip);
    end_op();
    return -1;
  }

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
    itrunc(ip);   // truncate to zero inside the current transaction.
    iupdate(ip);
  }

  iunlock(ip);        // the global file now owns this inode reference.
  end_op();
  return fd;
}

// sys_mknod: create a device node (type T_DEVICE) at `path`.
uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  if (argstr(0, path, MAXPATH) < 0 || argint(1, &major) < 0 ||
      argint(2, &minor) < 0)
    return -1;

  begin_op();
  ip = create(path, T_DEVICE, (short)major, (short)minor);
  if (ip == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

// sys_mkdir: create a directory at `path`.
uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  ip = create(path, T_DIR, 0, 0);
  if (ip == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

// sys_chdir: change the current working directory. Publishes only committed
// namespace state; the previous cwd reference is released after the new one
// is validated and locked.
uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0)
    return -1;

  ilock(ip);
  if (ip->type != T_DIR) {
    iunlockput(ip);
    return -1;
  }
  iunlock(ip);
  if (p->cwd)
    iput(p->cwd);   // release the previous current-directory reference.
  p->cwd = ip;
  return 0;
}

// sys_link: create a hard link at path2 naming the inode at path1.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = namei(old)) == 0) {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR) {
    iunlockput(ip);   // cannot hard-link a directory.
    end_op();
    return -1;
  }
  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);
  end_op();
  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// sys_unlink: remove the named directory entry via the inode unlink_helper.
// Publishes only a committed namespace change; missing, nonempty, or invalid
// paths return -1 without a partial directory mutation.
uint64
sys_unlink(void)
{
  char path[MAXPATH];

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if (unlink_helper(path, 0, 0) < 0) {
    end_op();
    return -1;
  }
  end_op();
  return 0;
}

// sys_fstat: fill the user `struct stat` for the open file `fd`, aggregating
// kernel/inode.stati through the validated copyout boundary.
uint64
sys_fstat(void)
{
  struct file *f;
  int fd;
  uint64 st;

  if (argint(0, &fd) < 0 || fd < 0 || fd >= NOFILE)
    return -1;
  if (argaddr(1, &st) < 0)
    return -1;
  if ((f = myproc()->ofile[fd]) == 0)
    return -1;
  return filestat(f, st);
}

// sys_read: read up to n bytes from the open file `fd` into user `addr`.
uint64
sys_read(void)
{
  struct file *f;
  struct proc *p = myproc();
  int fd, n;
  uint64 addr;

  if (argint(0, &fd) < 0 || fd < 0 || fd >= NOFILE)
    return -1;
  if (argaddr(1, &addr) < 0)
    return -1;
  if (argint(2, &n) < 0)
    return -1;
  if ((f = p->ofile[fd]) == 0)
    return -1;
  if (n <= 0)
    return 0;
  return fileread(f, addr, n);
}

// sys_write: write n bytes from user `addr` to file descriptor `fd`. When fd
// is the console (1) and no open file is published in that slot (the Lab 5
// first-process path), the bytes are emitted on the console; otherwise the
// write goes through the file layer to the open file's validated user buffer.
uint64
sys_write(void)
{
  struct file *f;
  struct proc *p = myproc();
  int fd, n, i;
  uint64 addr;
  char kbuf[64];

  if (argint(0, &fd) < 0 || fd < 0 || fd >= NOFILE)
    return -1;
  if (argaddr(1, &addr) < 0)
    return -1;
  if (argint(2, &n) < 0)
    return -1;
  if (n <= 0)
    return 0;

  f = p->ofile[fd];
  if (f == 0 && fd == 1) {
    // Lab 5 console fallback used by the first user process before a device
    // file is published; never dereferenced, copies through validated pages.
    if (n > (int)sizeof(kbuf))
      n = sizeof(kbuf);
    if (copyin(p->pagetable, kbuf, addr, (uint64)n) < 0)
      return -1;
    for (i = 0; i < n; i++)
      consoleputc(kbuf[i]);
    return n;
  }
  if (f == 0)
    return -1;
  return filewrite(f, addr, n);
}

// sys_close: clear the descriptor before releasing its file reference; the
// final close releases the underlying inode reference exactly once.
uint64
sys_close(void)
{
  struct proc *p = myproc();
  int fd;
  struct file *f;

  if (argint(0, &fd) < 0 || fd < 0 || fd >= NOFILE)
    return -1;
  if ((f = p->ofile[fd]) == 0)
    return -1;   // invalid descriptor: leave unrelated descriptors unchanged.
  p->ofile[fd] = 0;   // clear before releasing the reference.
  fileclose(f);
  return 0;
}

// sys_dup: duplicate descriptor `fd` into the lowest free slot, sharing the
// same global file (and therefore its serialized offset) via one extra ref.
uint64
sys_dup(void)
{
  struct file *f;
  struct proc *p = myproc();
  int fd, nd;

  if (argint(0, &fd) < 0 || fd < 0 || fd >= NOFILE)
    return -1;
  if ((f = p->ofile[fd]) == 0)
    return -1;
  if ((nd = fdalloc(f)) < 0)
    return -1;
  filedup(f);   // the duplicated slot shares one extra file reference.
  return nd;
}
