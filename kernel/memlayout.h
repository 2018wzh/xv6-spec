// Physical memory layout of the Lab 2 bootstrap slice.

// qemu -machine virt places the ns16550a UART at this physical address.
#define UART0 0x10000000L

// qemu -machine virt with 128M RAM ranges up to this physical address.
#define PHYSTOP 0x80000000L + 128L * 1024L * 1024L

// The early boot stack lives in bss and is sized for a single boot hart.
extern char bootstacktop[];