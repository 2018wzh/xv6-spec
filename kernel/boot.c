// boot.c - Lab 2 deterministic serial banner publication over the
// QEMU virt ns16550a byte-addressed UART.

#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "defs.h"

// Byte offsets within the ns16550a UART MMIO region. These are
// byte-addressed registers and are never scaled by a C word size.
#define UART_THR 0              // Transmit Holding Register
#define UART_LSR 5              // Line Status Register
#define LSR_TX_READY (1 << 5)   // Transmitter Holding Register Empty

// Return the immutable Lab 2 banner. Precondition: the boot hart has
// entered supervisor mode with the linked image accessible through the
// configured PMP entry (stage-transition faults occur before this call).
const char *
boot_banner(void)
{
  return "XV6_BOOT_OK\n";
}

// Write boot_banner to the QEMU virt ns16550a exactly once, polling the
// byte-addressed LSR transmitter-ready bit before each byte write.
void
publish_boot_banner(void)
{
  volatile uchar *uart = (volatile uchar *)UART0;
  const char *s;

  for (s = boot_banner(); *s != 0; ++s) {
    // Wait for the transmitter to be ready to accept a byte.
    while ((uart[UART_LSR] & LSR_TX_READY) == 0)
      ;
    uart[UART_THR] = (uchar)*s;
  }
}