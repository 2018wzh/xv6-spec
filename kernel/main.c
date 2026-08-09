#include "types.h"
#include "riscv.h"
#include "defs.h"
void main(void) { kinit(); kvminit(); kvminithart(); kernel_main(); for (;;) {} }
