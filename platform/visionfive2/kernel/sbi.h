#ifndef XV6_SBI_H
#define XV6_SBI_H

#include "types.h"

struct sbiret {
  long error;
  long value;
};

#define SBI_EXT_BASE 0x10
#define SBI_EXT_TIME 0x54494d45
#define SBI_EXT_IPI  0x735049
#define SBI_EXT_RFENCE 0x52464e43
#define SBI_EXT_HSM  0x48534d
#define SBI_EXT_SRST 0x53525354

struct sbiret sbi_call(long ext, long fid, uint64 arg0, uint64 arg1,
                       uint64 arg2, uint64 arg3, uint64 arg4, uint64 arg5);
int sbi_probe_extension(long ext);
int sbi_set_timer(uint64 deadline);
int sbi_send_ipi(uint64 hart_mask, uint64 hart_mask_base);
int sbi_remote_sfence_vma(uint64 hart_mask, uint64 hart_mask_base,
                          uint64 start, uint64 size);
int sbi_hart_start(uint64 hartid, uint64 start_addr, uint64 opaque);
void sbi_system_reset(uint32 type, uint32 reason) __attribute__((noreturn));

#endif
