// uart.c - ns16550a UART driver for the QEMU virt board.
//
// The ns16550a is byte-addressed MMIO. THR/RHR is byte offset 0 and LSR is
// byte offset 5 with transmitter-ready bit 5 and receiver-ready bit 0. Register
// offsets are never scaled by a C word size.
//
// Trap-stage uartinit idempotently re-establishes the same divisor and 8-bit
// line control that boot's polling banner publication relied on, then enables
// FIFO receive interrupts. After this handoff, console and printk own ordinary
// output (single boot hart, Lab 4).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

#define Reg(reg) ((volatile uchar *)(UART0 + reg))

// the UART control registers are memory-mapped at offset UART0.
#define RHR 0   // receive holding register (for input bytes)
#define THR 0   // transmit holding register (for output bytes)
#define IER 1   // interrupt enable register
#define IER_RX_ENABLE (1 << 0)
#define IER_TX_ENABLE (1 << 1)
#define FCR 2      // FIFO control register
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR  (3 << 1)
#define LCR 3      // line control register
#define LCR_EIGHT_BITS  (3 << 0)
#define LCR_BAUD_LATCH  (1 << 7)
#define LSR 5      // line status register
#define LSR_RX_READY (1 << 0)
#define LSR_TX_READY (1 << 5)

// Configure the UART for 8-bit output and re-establish the boot-stage line
// control, then enable FIFO receive interrupts. Must be called once before
// UART interrupts are enabled.
void
uartinit(void)
{
  // Disable interrupts while we re-establish the line configuration.
  *Reg(IER) = 0x00;

  // Idempotently re-establish the boot's divisor (38400 baud) and 8-bit
  // line control, matching the banner publication path that preceded this
  // handoff.
  *Reg(LCR) = LCR_BAUD_LATCH;
  *Reg(0) = 1;   // low byte of the divisor.
  *Reg(1) = 0;   // high byte of the divisor.
  *Reg(LCR) = LCR_EIGHT_BITS;

  *Reg(FCR) = FCR_FIFO_ENABLE | FCR_FIFO_CLEAR;

  // Enable transmit and receive interrupts after the line is configured.
  *Reg(IER) = IER_TX_ENABLE | IER_RX_ENABLE;
}

// Write one byte to the UART transmittter synchronously, waiting for the
// byte-addressed LSR transmitter-ready bit before each write. Used for
// bounded diagnostic output in trap context and console output.
void
uartputc_sync(int c)
{
  int not_ready;

  // Wait for the LSR transmitter-ready bit (bit 5 of byte offset 5).
  do {
    not_ready = (*Reg(LSR) & LSR_TX_READY) == 0;
  } while (not_ready);

  *Reg(THR) = (uchar)c;
}

// Read one byte from the UART receive FIFO, or -1 when no byte is ready.
int
uartgetc(void)
{
  if (*Reg(LSR) & LSR_RX_READY) {
    int c = *Reg(RHR);
    return c;
  }
  return -1;
}

// Called from PLIC dispatch when the UART IRQ is the active interrupt source.
// Consumes every currently available byte and returns; it does not block and
// never attempts to read a byte that LSR does not report as ready.
void
uartintr(void)
{
  while (1) {
    int c = uartgetc();
    if (c < 0)
      break;
    // Process the received character at the console layer.
    consoleintr(c);
  }
}
