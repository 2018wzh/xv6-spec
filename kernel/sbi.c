#include "types.h"
#include "sbi.h"

struct sbiret
sbi_call(long ext, long fid, uint64 arg0, uint64 arg1, uint64 arg2,
         uint64 arg3, uint64 arg4, uint64 arg5)
{
  register uint64 a0 asm("a0") = arg0;
  register uint64 a1 asm("a1") = arg1;
  register uint64 a2 asm("a2") = arg2;
  register uint64 a3 asm("a3") = arg3;
  register uint64 a4 asm("a4") = arg4;
  register uint64 a5 asm("a5") = arg5;
  register long a6 asm("a6") = fid;
  register long a7 asm("a7") = ext;
  asm volatile("ecall"
               : "+r"(a0), "+r"(a1)
               : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
               : "memory");
  return (struct sbiret){.error = (long)a0, .value = (long)a1};
}

int
sbi_probe_extension(long ext)
{
  struct sbiret ret = sbi_call(SBI_EXT_BASE, 3, ext, 0, 0, 0, 0, 0);
  return ret.error == 0 && ret.value != 0;
}

int
sbi_set_timer(uint64 deadline)
{
  return (int)sbi_call(SBI_EXT_TIME, 0, deadline, 0, 0, 0, 0, 0).error;
}

int
sbi_send_ipi(uint64 hart_mask, uint64 hart_mask_base)
{
  return (int)sbi_call(SBI_EXT_IPI, 0, hart_mask, hart_mask_base,
                       0, 0, 0, 0).error;
}

int
sbi_remote_sfence_vma(uint64 hart_mask, uint64 hart_mask_base,
                      uint64 start, uint64 size)
{
  return (int)sbi_call(SBI_EXT_RFENCE, 1, hart_mask, hart_mask_base,
                       start, size, 0, 0).error;
}

int
sbi_hart_start(uint64 hartid, uint64 start_addr, uint64 opaque)
{
  return (int)sbi_call(SBI_EXT_HSM, 0, hartid, start_addr, opaque, 0, 0, 0).error;
}

void
sbi_system_reset(uint32 type, uint32 reason)
{
  sbi_call(SBI_EXT_SRST, 0, type, reason, 0, 0, 0, 0);
  for (;;)
    asm volatile("wfi");
}
