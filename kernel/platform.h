#ifndef XV6_PLATFORM_H
#define XV6_PLATFORM_H

#include "types.h"
#include "param.h"

enum platform_block_kind {
  PLATFORM_BLOCK_VIRTIO,
  PLATFORM_BLOCK_JH7110_SD,
};

struct platform_info {
  const char *name;
  uint64 kernel_base;
  uint64 ram_base;
  uint64 ram_end;
  uint64 uart_base;
  uint64 plic_base;
  uint64 block_base;
  uint32 uart_reg_shift;
  uint32 uart_reg_width;
  uint32 uart_irq;
  uint32 block_irq;
  uint32 timebase_frequency;
  uint64 hart_ids[NCPU];
  int hart_count;
  int uses_sbi;
  enum platform_block_kind block_kind;
};

void platform_early_init(uint64 hartid, uint64 dtb);
const struct platform_info *platform_get(void);
uint64 platform_hartid(int cpu_index);
int platform_cpu_index(uint64 hartid);
void platform_start_harts(uint64 entry);
void platform_set_timer(uint64 deadline);
void platform_shutdown(void) __attribute__((noreturn));

#endif
