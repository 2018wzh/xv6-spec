#include "types.h"

// Return the boot banner string.
const char *
boot_banner(void)
{
  return "xv6 kernel is booting\n";
}

// Output a single character via SBI legacy console putchar (EID #1).
void
console_putchar(int c)
{
  register int a0 asm("a0") = c;
  register long a6 asm("a6") = 0;
  register long a7 asm("a7") = 1;
  asm volatile("ecall" : "+r"(a0) : "r"(a6), "r"(a7) : "memory");
}

// Write a null-terminated string to the console (up to n bytes).
void
console_write(const char *buf, int n)
{
  for (int i = 0; i < n && buf[i]; i++)
    console_putchar(buf[i]);
}

// Halt the system via SBI System Reset extension (EID #0x53525354).
void
shutdown(void)
{
  register long a0 asm("a0") = 0;
  register long a1 asm("a1") = 0;
  register long a6 asm("a6") = 0;
  register long a7 asm("a7") = 0x53525354;
  asm volatile("ecall" : : "r"(a0), "r"(a1), "r"(a6), "r"(a7) : "memory");
  for (;;)
    asm volatile("wfi");
}

// Kernel C entry point. Prints the boot banner.
// Runtime boot path uses main() in main.c per xv6-spec contract.
void
kernel_main(void)
{
  console_write(boot_banner(), 128);
}
