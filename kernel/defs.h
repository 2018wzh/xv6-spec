// defs.h - Function declarations shared across the Lab 2 kernel bootstrap.

// string.c
int      strcmp(const char *, const char *);
int      strncmp(const char *, const char *, uint);
char*    strcpy(char *, const char *);
char*    strncpy(char *, const char *, int);
char*    safestrcpy(char *, const char *, int);
int      strlen(const char *);
void*    memset(void *, int, uint);
void*    memmove(void *, const void *, uint);
int      memcmp(const void *, const void *, uint);
void*    memcpy(void *, const void *, uint);

// boot.c
const char* boot_banner(void);
void        publish_boot_banner(void);

// start.c
void     start(void);

// main.c
void     main(void);

// spinlock.c
struct spinlock;
struct context;
struct proc;
void     initlock(struct spinlock*, const char*);
void     acquire(struct spinlock*);
void     release(struct spinlock*);
int      holding(struct spinlock*);

// kalloc.c
void     kinit(void);
void*    kalloc(void);
void     kfree(void*);

// vm.c
void     kvminit(void);
void     kvminithart(void);
pagetable_t uvmcreate(void);
void     uvmfree(pagetable_t, uint64);
void     uvmmap(pagetable_t, uint64, uint64, uint64, int);
int      uvmfirst(pagetable_t, uchar *, uint);
uint64   uvmalloc(pagetable_t, uint64, uint64, int);
uint64   uvmdealloc(pagetable_t, uint64, uint64);
int      uvmcopy(pagetable_t, pagetable_t, uint64);
void     uvmunmap(pagetable_t, uint64, uint64, int);
void     freewalk(pagetable_t);
int      copyin(pagetable_t, char *, uint64, uint64);
int      copyinstr(pagetable_t, char *, uint64, uint64);
int      copyout(pagetable_t, uint64, char *, uint64);
uint64   walkaddr(pagetable_t, uint64);

// proc.c
void     procinit(void);
struct proc *allocproc(void);
void     scheduler(void);
void     sched(void);
void     yield(void);
void     sleep(void *, struct spinlock *);
void     wakeup(void *);
struct proc *myproc(void);
void     swtch(struct context *, struct context *);
void     userinit(void);
int      fork(void);
void     exit(int) __attribute__((noreturn));
int      wait(uint64);
int      kill(int);
int      exec(char *, char **);

// trap.c
void     trapinit(void);
void     kerneltrap(struct trapframe *);
int      devintr(void);
extern void kernelvec(void);
void     usertrap(void);
void     usertrapret(void);
extern void uservec(void);
extern void userret(void);

// syscall.c / sysproc.c
void     syscall(void);
int      fetchaddr(uint64, uint64 *);
int      fetchargint(int, uint64 *);
int      fetchstr(uint64, char *, int);
int      argint(int, int *);
int      argaddr(int, uint64 *);
int      argstr(int, char *, int);

// plic.c
void     plicinit(void);
void     plicinithart(void);
int      plic_claim(void);
void     plic_complete(int);

// uart.c
void     uartinit(void);
void     uartputc_sync(int);
int      uartgetc(void);
void     uartintr(void);

// console.c
void     consoleinit(void);
void     consoleputc(int);
void     consoleintr(int);

// printk.c
void     printf(char *, ...);

// string.c
void     panic(char*);

// virtio_disk.c
void     virtio_disk_init(void);
void     virtio_disk_rw(uint64, void *, int);
void     virtio_disk_intr(void);

// log.c (kernel/log)
struct buf;
void     initlog(int);
void     begin_op(void);
void     log_write(struct buf *);
void     end_op(void);

// fs.c (kernel/inode)
void     fsinit(int);

// file.c (kernel/file)
struct file;
void         fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *);
void         fileclose(struct file *);
int          filestat(struct file *, uint64);
int          fileread(struct file *, uint64, int);
int          filewrite(struct file *, uint64, int);

// pipe.c (kernel/pipe)
struct file;
struct pipe;
int          pipealloc(struct file **, struct file **);
void         pipeclose(struct pipe *, int);
int          pipewrite(struct pipe *, uint64, int);
int          piperead(struct pipe *, uint64, int);

// sysfile.c (kernel/file) syscall handlers
uint64   sys_open(void);
uint64   sys_read(void);
uint64   sys_write(void);
uint64   sys_close(void);
uint64   sys_fstat(void);
uint64   sys_dup(void);
uint64   sys_mknod(void);
uint64   sys_mkdir(void);
uint64   sys_chdir(void);
uint64   sys_link(void);
uint64   sys_unlink(void);
uint64   sys_pipe(void);

// riscv.h interrupt helpers
void     intr_on(void);
void     intr_off(void);
int      intr_get(void);

// physical memory delimiters
extern char end[];
extern char etext[];
extern char trampoline[];   // kernel.ld: the shared trampoline page
