#include "types.h"
#include "defs.h"
void main(void) { kinit(); kvminit(); kvminithart(); kernel_main(); for (;;) {} }
