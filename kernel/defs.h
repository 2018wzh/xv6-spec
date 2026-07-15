struct spinlock;
// boot.c
const char* boot_banner(void);
void console_putchar(int);
void console_write(const char*, int);
void shutdown(void);
void kernel_main(void);
// string.c
void* memset(void*, int, uint);
int strlen(const char*);
