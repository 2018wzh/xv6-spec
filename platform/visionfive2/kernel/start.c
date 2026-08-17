#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "platform.h"

void main();
void timerinit();
void kernelvec(void);
extern void _entry_secondary(void);
extern char bss[];
extern char end[];

static void
zerobss(void)
{
  for (char *p = bss; p < end; ++p)
    *p = 0;
}

// entry.S needs one stack per CPU.
__attribute__((aligned(16))) char stack0[4096 * NCPU];

static volatile int platform_ready;

// entry.S jumps here in machine mode on QEMU.
void
start(uint64 hartid, uint64 dtb)
{
  if (hartid == 0) {
    zerobss();
    platform_early_init(hartid, dtb);
    __atomic_store_n(&platform_ready, 1, __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(&platform_ready, __ATOMIC_ACQUIRE) == 0)
      ;
  }

  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  w_mepc((uint64)main);

  // disable paging for now.
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // ask for clock interrupts.
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = platform_cpu_index(hartid);
  if (id < 0)
    for (;;)
      ;
  w_tp(id);

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

#ifdef PLATFORM_VISIONFIVE2
void
supervisor_start(uint64 hartid, uint64 dtb)
{
  w_satp(0);
  zerobss();
  platform_early_init(hartid, dtb);
  w_tp(0);
  // Install the supervisor trap vector before paging is enabled. A handoff
  // trap with stvec==0 is an invisible hang on physical hardware.
  w_stvec((uint64)kernelvec);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
  platform_set_timer(r_time() + platform_get()->timebase_frequency / 10);
  // Secondary harts are started later, only after the boot hart has finished
  // fsinit/kexec for the first user process (see forkret).
  main();
}

void
supervisor_secondary_start(uint64 hartid, uint64 cpu_index)
{
  if (platform_hartid((int)cpu_index) != hartid)
    platform_shutdown();
  w_satp(0);
  w_tp(cpu_index);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
  platform_set_timer(r_time() + platform_get()->timebase_frequency / 10);
  main();
}
#endif

// ask each hart to generate timer interrupts.
void
timerinit()
{
  // enable the sstc extension (i.e. stimecmp).
  w_menvcfg(r_menvcfg() | (1L << 63));

  // allow supervisor to use stimecmp and time.
  w_mcounteren(r_mcounteren() | 2);

  // ask for the very first timer interrupt.
  platform_set_timer(r_time() + platform_get()->timebase_frequency / 10);
}
