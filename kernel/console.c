#include "types.h"
#include "riscv.h"
#include "defs.h"
void consoleinit(void) { uartinit(); }
void consputc(int c) { uartputc_sync(c); }
void consoleintr(int c) { if(c >= 0) consputc(c); }
