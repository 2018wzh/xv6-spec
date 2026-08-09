#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#define R(reg) ((volatile unsigned char *)(UART0 + (reg)))
void uartinit(void) { *R(1) = 0; *R(3) = 0x80; *R(0) = 3; *R(1) = 0; *R(3) = 3; *R(2) = 7; *R(1) = 1; }
void uartputc_sync(int c) { while((*R(5) & 0x20) == 0) {} *R(0) = c; }
void uartputc(int c) { uartputc_sync(c); }
int uartgetc(void) { return (*R(5) & 1) ? *R(0) : -1; }
void uartintr(void) { int c; while((c = uartgetc()) >= 0) consoleintr(c); }
