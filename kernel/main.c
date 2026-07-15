#include "types.h"
#include "defs.h"
void main(void) { consoleinit(); printkinit(); uartinit(); plicinit(); plicinithart(); kernel_main(); for (;;) {} }
