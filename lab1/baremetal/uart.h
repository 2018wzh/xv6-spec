#ifndef VOS_LAB1_UART_H
#define VOS_LAB1_UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_putstr(const char *s);

#endif
