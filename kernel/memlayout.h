// Physical memory layout of the Lab 2/3 bootstrap slice.

#define KERNBASE 0x80000000L // kernel linked at this physical address.

// qemu -machine virt places the ns16550a UART at this physical address.
#define UART0 0x10000000L

// qemu -machine virt places the PLIC at this physical address.
#define PLIC 0x0c000000L

// qemu -machine virt first virtio mmio disk interface.
#define VIRTIO0 0x10001000L

// qemu -machine virt UART interrupt source number delivered via the PLIC.
#define UART0_IRQ 10

// qemu -machine virt with 128M RAM ranges up to this physical address.
#define PHYSTOP 0x80000000L + 128L * 1024L * 1024L

// The early boot stack lives in bss and is sized for a single boot hart.
extern char bootstacktop[];

// Page size and Sv39 page-table geometry.
#define PGSIZE 4096        // bytes per page
#define PGSHIFT 12         // log2(PGSIZE)
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

// Sv39 page table entry bits and helpers (supervisor/device mappings must
// never set PTE_U).
#define PTE_V (1L << 0)   // valid
#define PTE_R (1L << 1)   // readable
#define PTE_W (1L << 2)   // writable
#define PTE_X (1L << 3)   // executable
#define PTE_U (1L << 4)   // user accessible (kernel mappings never set it)

#define PTE2PA(pte) (((pte) >> 10) << 12)
#define PA2PTE(pa) (((uint64)(pa) >> 12) << 10)
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

#define PXMASK 0x1FF // 9 bits
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va) ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

#define MAXVA (1L << (9 + 9 + 9 + 12))   // top of the Sv39 kernel space
#define TRAMPOLINE (MAXVA - PGSIZE)      // highest kernel virtual page