// riscv.h - RISC-V RV64 machine/supervisor control register and CSR helpers.

#ifndef __ASSEMBLER__

// Supervisor register (sstatus), bits as defined by the RISC-V spec.
#define SSTATUS_SPP (1L << 8)   // Previous mode, 1 = Supervisor, 0 = User
#define SSTATUS_SPIE (1L << 5)  // Supervisor Previous Interrupt Enable
#define SSTATUS_UPIE (1L << 4)  // User Previous Interrupt Enable
#define SSTATUS_SIE (1L << 1)   // Supervisor Interrupt Enable
#define SSTATUS_UIE (1L << 0)   // User Interrupt Enable

// Machine Status Register (mstatus) fields needed before mret.
#define MSTATUS_MPP (3L << 11)  // Previous mode: 11 = Machine, 01 = Supervisor
#define MSTATUS_MPP_S (1L << 11) // 01 = Supervisor (previous mode)
#define MSTATUS_MIE (1L << 3)   // Machine Interrupt Enable

// Supervisor Exception Program Counter (sepc).
// Supervisor Interrupt Enable (sie).
#define SIE_SEIE (1L << 9)   // external
#define SIE_STIE (1L << 5)   // timer
#define SIE_SSIE (1L << 1)   // software

// Machine mode interruption enable register bits.
#define MIE_MEIE (1L << 11)  // external
#define MIE_MTIE (1L << 7)   // timer
#define MIE_MSIE (1L << 3)   // software

// CLINT MMIO (machine-level timer and interrupt controller).
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // cycles since boot.

// Physical memory protection CSR encodings (RV64).
#define CSRPMPCFG0  0x3A0  // pmpcfg0, holds pmp0cfg
#define CSRPMPADDR0 0x3B0  // pmpaddr0, holds pmp0addr (NOT 0x3A3 = pmpcfg3)

// pmpcfg0 field bits for entry 0 (low byte).
#define PMP_R      (1L << 0)
#define PMP_W      (1L << 1)
#define PMP_X      (1L << 2)
#define PMP_A      (3L << 3)  // mode bits (A field)
#define PMP_A_NAPOT (3L << 3) // NAPOT addressing
#define PMP_L      (1L << 7)  // lock (outside Lab 2 scope, kept clear)

// CSR read/write helpers.
#define r_sstatus() ({ \
  uint64 x; \
  asm volatile("csrr %0, sstatus" : "=r" (x) ); \
  x; })
#define w_sstatus(x) asm volatile("csrw sstatus, %0" : : "r" (x))

#define r_mstatus() ({ \
  uint64 x; \
  asm volatile("csrr %0, mstatus" : "=r" (x) ); \
  x; })
#define w_mstatus(x) asm volatile("csrw mstatus, %0" : : "r" (x))

#define r_sepc() ({ \
  uint64 x; \
  asm volatile("csrr %0, sepc" : "=r" (x) ); \
  x; })
#define w_sepc(x) asm volatile("csrw sepc, %0" : : "r" (x))

#define w_mepc(x) asm volatile("csrw mepc, %0" : : "r" (x))

#define mret() asm volatile("mret")

#define r_mhartid() ({ \
  uint64 x; \
  asm volatile("csrr %0, mhartid" : "=r" (x) ); \
  x; })

#define w_pmpcfg0(x) asm volatile("csrw 0x3a0, %0" : : "r" (x))
#define w_pmpaddr0(x) asm volatile("csrw 0x3b0, %0" : : "r" (x))

// Supervisor address translation and protection (satp) + TLB shootdown.
#define w_satp(x) do { asm volatile("csrw satp, %0" : : "r" (x)); } while (0)
#define sfence_vma() asm volatile("sfence.vma zero, zero")
// Sv39 mode (8) shifted into the satp MODE field.
#define MAKE_SATP(pagetable) (((uint64)pagetable >> 12) | (8L << 60))

static inline uint64
r_pmpcfg0(void)
{
  uint64 x;
  asm volatile("csrr %0, 0x3a0" : "=r" (x));
  return x;
}

static inline uint64
r_pmpaddr0(void)
{
  uint64 x;
  asm volatile("csrr %0, 0x3b0" : "=r" (x));
  return x;
}

#endif // __ASSEMBLER__