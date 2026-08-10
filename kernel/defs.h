// defs.h - Function declarations shared across the Lab 2 kernel bootstrap.

// string.c
int      strcmp(const char *, const char *);
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

// trap.c
void     trapinit(void);
void     kerneltrap(struct trapframe *);
int      devintr(void);
extern void kernelvec(void);

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

// riscv.h interrupt helpers
void     intr_on(void);
void     intr_off(void);
int      intr_get(void);

// physical memory delimiters
extern char end[];
extern char etext[];