#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "buf.h"
#include "proc.h"
#include "fs.h"
#include "elf.h"
#include "defs.h"

static int
flags2perm(int flags)
{
  int perm = 0;
  if (flags & ELF_PROG_FLAG_EXEC) perm |= PTE_X;
  if (flags & ELF_PROG_FLAG_WRITE) perm |= PTE_W;
  if (flags & ELF_PROG_FLAG_READ) perm |= PTE_R;
  return perm;
}

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off, argc = 0;
  uint64 sz = 0, sp, stackbase, ustack[MAXARG + 1];
  struct elfhdr elf;
  struct proghdr ph;
  struct inode *ip;
  struct proc *p = myproc();
  pagetable_t pagetable = 0;

  begin_op();
  if ((ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf) ||
      elf.magic != ELF_MAGIC)
    goto bad;
  if ((pagetable = uvmcreate()) == 0)
    goto bad;
  uvmmap(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  uvmmap(pagetable, TRAPFRAME, p->trapframe, PGSIZE, PTE_R | PTE_W | PTE_U);
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
    uint64 va;
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz || ph.vaddr + ph.memsz < ph.vaddr ||
        ph.vaddr % PGSIZE != 0)
      goto bad;
    if ((sz = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz,
                       flags2perm(ph.flags))) == 0)
      goto bad;
    for (va = 0; va < ph.filesz; va += PGSIZE) {
      uint n = ph.filesz - va < PGSIZE ? ph.filesz - va : PGSIZE;
      uint64 pa = walkaddr(pagetable, ph.vaddr + va);
      if (pa == 0 || readi(ip, 0, pa, ph.off + va, n) != n)
        goto bad;
    }
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  sz = PGROUNDUP(sz);
  if ((sz = uvmalloc(pagetable, sz, sz + 2 * PGSIZE, PTE_R | PTE_W)) == 0)
    goto bad_nolog;
  uvmunmap(pagetable, sz - 2 * PGSIZE, 1, 1);
  sp = sz;
  stackbase = sp - PGSIZE;
  for (argc = 0; argv[argc]; argc++) {
    if (argc >= MAXARG)
      goto bad_nolog;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16;
    if (sp < stackbase || copyout(pagetable, sp, argv[argc],
                                  strlen(argv[argc]) + 1) < 0)
      goto bad_nolog;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;
  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase || copyout(pagetable, sp, (char *)ustack,
                                (argc + 1) * sizeof(uint64)) < 0)
    goto bad_nolog;

  last = path;
  for (s = path; *s; s++)
    if (*s == '/') last = s + 1;
  safestrcpy(p->name, last, sizeof(p->name));
  if (p->pagetable) {
    uvmunmap(p->pagetable, TRAPFRAME, 1, 0);
    uvmunmap(p->pagetable, TRAMPOLINE, 1, 0);
    uvmfree(p->pagetable, p->sz);
  }
  p->pagetable = pagetable;
  p->sz = sz;
  ((struct usertrapframe *)p->trapframe)->epc = elf.entry;
  ((struct usertrapframe *)p->trapframe)->sp = sp;
  ((struct usertrapframe *)p->trapframe)->a1 = sp;
  return argc;

bad:
  if (ip) iunlockput(ip);
  end_op();
bad_nolog:
  if (pagetable) {
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, sz);
  }
  return -1;
}
