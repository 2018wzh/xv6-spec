#include "types.h"
static volatile unsigned char *const serial = (unsigned char *)0x10000000L;
const char *boot_banner(void) { return "XV6_BOOT_OK lab2\n"; }
static void putc(int c) { while((serial[5] & 0x20) == 0) {} serial[0] = c; }
void kernel_main(void) { const char *p = boot_banner(); while(*p) putc(*p++); }
void panic(char *message) { while(*message) putc(*message++); for(;;) {} }
