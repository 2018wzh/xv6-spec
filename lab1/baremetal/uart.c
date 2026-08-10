// Minimal 16550-compatible UART driver for the QEMU RISC-V virt machine.
// The virt machine places the 16550 console at physical 0x10000000.
#include <stdint.h>

#include "uart.h"

#define UART_BASE 0x10000000UL

#define UART_THR 0         // transmit holding register (write)
#define UART_RBR 0         // receive buffer register (read)
#define UART_IER 1         // interrupt enable
#define UART_FCR 2         // FIFO control
#define UART_LCR 3         // line control
#define UART_LSR 5         // line status
#define UART_DLL 0         // divisor latch low (DLAB=1)
#define UART_DLM 1         // divisor latch high (DLAB=1)

#define LCR_DLAB 0x80u
#define LCR_8N1 0x03u
#define FCR_ENABLE_AND_CLEAR 0x07u
#define LSR_THRE 0x20u     // transmit holding register empty

static inline volatile uint8_t *uart_reg(int offset) {
    return (volatile uint8_t *)(UART_BASE + (unsigned long)offset);
}

void uart_init(void) {
    volatile uint8_t *lcr = uart_reg(UART_LCR);
    uint8_t saved = *lcr;
    // Enter divisor-latch access mode and program the baud divisor. The virt
    // 16550 clock is 3,686,400 Hz; divisor 2 selects 115200 baud.
    *lcr = LCR_DLAB;
    *uart_reg(UART_DLL) = 0x02;
    *uart_reg(UART_DLM) = 0x00;
    // Leave DLAB, select 8 data bits, no parity, 1 stop bit.
    *lcr = (saved & ~LCR_DLAB) | LCR_8N1;
    *uart_reg(UART_FCR) = FCR_ENABLE_AND_CLEAR;
}

void uart_putc(char c) {
    // Wait until the transmit holding register is empty.
    while ((*uart_reg(UART_LSR) & LSR_THRE) == 0) {
    }
    *uart_reg(UART_THR) = (uint8_t)c;
}

void uart_puts(const char *s) {
    for (; s && *s; s++) {
        uart_putc(*s);
    }
}

void uart_putstr(const char *s) {
    uart_puts(s);
}
