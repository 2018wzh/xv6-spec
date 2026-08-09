#include "types.h"
#include "riscv.h"
#include "defs.h"
void main(void) { consoleinit(); printkinit(); uartinit(); trapinit(); trapinithart(); plicinit(); plicinithart(); printk((char*)boot_banner()); for (;;) {} }
