#include <stdarg.h>
#include "types.h"
#include "riscv.h"
#include "defs.h"
void printkinit(void) {}
int printk(char *fmt, ...) { int n = 0; while(*fmt) { consputc(*fmt++); n++; } return n; }
void panic(char *message) { intr_off(); printk("panic: "); printk(message); printk("\n"); for(;;) {} }
