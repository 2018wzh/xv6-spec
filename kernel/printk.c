// printk.c - bounded synchronous formatted output for the kernel console.
//
// printf writes through the console lock to the UART. In trap context the
// interrupt state is already disabled (saved in the trap frame), and the
// console lock's interrupt-preserving spinlock makes output safe from normal
// context too. Diagnostic output is bounded and synchronous, so it is
// well-defined inside kerneltrap.

#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include <stdarg.h>

static char digits[] = "0123456789abcdef";

static void
printint(int xx, int base, int sign)
{
  char buf[16];
  int i;
  uint x;

  if (sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    consoleputc(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;
  consoleputc('0');
  consoleputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consoleputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. Understands %d, %x, %p, %s, and %%.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c;
  char *s;

  va_start(ap, fmt);
  for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
    if (c != '%') {
      consoleputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if (c == 0)
      break;
    switch (c) {
      case 'd':
        printint(va_arg(ap, int), 10, 1);
        break;
      case 'x':
        printint(va_arg(ap, int), 16, 1);
        break;
      case 'p':
        printptr(va_arg(ap, uint64));
        break;
      case 's':
        if ((s = va_arg(ap, char *)) == 0)
          s = "(null)";
        for (; *s; s++)
          consoleputc(*s);
        break;
      case '%':
        consoleputc('%');
        break;
      default:
        // unknown format: print the offending char and a caret diagnostic.
        consoleputc('%');
        consoleputc(c);
        break;
    }
  }
  va_end(ap);
}