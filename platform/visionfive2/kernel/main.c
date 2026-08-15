#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if (cpuid() == 0) {
    consoleinit();
    printkinit();
    printk("%s", boot_banner());
    kinit();            // physical page allocator
    printk("main: kinit ok\n");
    kvminit();          // create kernel page table
    kvminithart();      // turn on paging
    printk("main: vm ok\n");
    procinit();         // process table
    trapinit();         // trap vectors
    trapinithart();     // install kernel trap vector
    plicinit();         // set up interrupt controller
    plicinithart();     // ask PLIC for device interrupts
    printk("main: proc/trap/plic ok\n");
    binit();            // buffer cache
    iinit();            // inode table
    fileinit();         // file table
    printk("main: fs tables ok\n");
    disk_init();        // platform block device
    printk("main: disk ok\n");
    userinit();         // first user process
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    started = 1;
  } else {
    while (started == 0)
      ;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    printk("hart %d starting\n", cpuid());
    kvminithart();  // turn on paging
    trapinithart(); // install kernel trap vector
    plicinithart(); // ask PLIC for device interrupts
  }

  scheduler();
}
