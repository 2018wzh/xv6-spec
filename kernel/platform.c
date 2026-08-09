#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "platform.h"
#include "sbi.h"
#include "third_party/libfdt/libfdt.h"

extern char end[];
extern void _entry_secondary(void);

static struct platform_info active;

#ifdef PLATFORM_VISIONFIVE2
static uint64
read_cells(const fdt32_t *cells, int count)
{
  uint64 value = 0;
  if (count < 1 || count > 2)
    panic("platform: unsupported DT cell count");
  for (int i = 0; i < count; i++)
    value = (value << 32) | fdt32_to_cpu(cells[i]);
  return value;
}

static uint64
node_reg(const void *fdt, int node, uint64 *size)
{
  int parent = fdt_parent_offset(fdt, node);
  int cells_len = 0;
  const fdt32_t *cells = fdt_getprop(fdt, parent, "#address-cells", &cells_len);
  int ac = cells && cells_len >= 4 ? fdt32_to_cpu(*cells) : 2;
  cells = fdt_getprop(fdt, parent, "#size-cells", &cells_len);
  int sc = cells && cells_len >= 4 ? fdt32_to_cpu(*cells) : 1;
  int len = 0;
  const fdt32_t *reg = fdt_getprop(fdt, node, "reg", &len);
  if (!reg || ac < 1 || ac > 2 || sc < 1 || sc > 2 ||
      len < (ac + sc) * (int)sizeof(fdt32_t))
    panic("platform: invalid DT reg");
  *size = read_cells(reg + ac, sc);
  return read_cells(reg, ac);
}

static uint32
node_u32(const void *fdt, int node, const char *name, uint32 fallback)
{
  int len = 0;
  const fdt32_t *value = fdt_getprop(fdt, node, name, &len);
  return value && len >= 4 ? fdt32_to_cpu(*value) : fallback;
}

static int
find_compatible(const void *fdt, const char *compatible)
{
  int node = -1;
  while ((node = fdt_next_node(fdt, node, 0)) >= 0)
    if (fdt_node_check_compatible(fdt, node, compatible) == 0)
      return node;
  panic("platform: required compatible missing");
}

static void
reserve_range(uint64 start, uint64 size)
{
  if (size == 0 || start + size < start)
    panic("platform: invalid reserved-memory range");
  uint64 kernel_end = PGROUNDUP((uint64)end);
  if (start <= kernel_end && kernel_end < start + size)
    panic("platform: kernel overlaps reserved memory");
  if (start > kernel_end && start < active.ram_end)
    active.ram_end = PGROUNDDOWN(start);
}

static void
parse_visionfive2(uint64 dtb)
{
  const void *fdt = (const void *)dtb;
  if (fdt_check_header(fdt) != 0)
    panic("platform: invalid DTB");
  if (fdt_node_check_compatible(fdt, 0, "starfive,visionfive-v2") != 0 &&
      fdt_node_check_compatible(fdt, 0,
        "starfive,visionfive-2-v1.3b") != 0 &&
      fdt_node_check_compatible(fdt, 0,
        "starfive,visionfive-2-v1.2a") != 0)
    panic("platform: unsupported board compatible");

  active.name = "starfive,jh7110-visionfive-2";
  active.kernel_base = KERNBASE;
  active.uses_sbi = 1;
  active.block_kind = PLATFORM_BLOCK_JH7110_SD;

  int memory = fdt_node_offset_by_prop_value(fdt, -1, "device_type",
                                              "memory", 7);
  uint64 memory_size = 0;
  active.ram_base = node_reg(fdt, memory, &memory_size);
  active.ram_end = active.ram_base + memory_size;
  if (active.ram_end < active.ram_base || active.ram_end <= (uint64)end)
    panic("platform: invalid RAM range");

  int reservation_count = fdt_num_mem_rsv(fdt);
  if (reservation_count < 0)
    panic("platform: invalid DT reservation map");
  for (int i = 0; i < reservation_count; i++) {
    uint64 start = 0, size = 0;
    if (fdt_get_mem_rsv(fdt, i, &start, &size) != 0)
      panic("platform: invalid DT reservation entry");
    reserve_range(start, size);
  }
  int reserved = fdt_path_offset(fdt, "/reserved-memory");
  if (reserved >= 0) {
    int child;
    fdt_for_each_subnode(child, fdt, reserved) {
      int property_len = 0;
      if (fdt_getprop(fdt, child, "reg", &property_len)) {
        uint64 size = 0;
        uint64 start = node_reg(fdt, child, &size);
        reserve_range(start, size);
        continue;
      }
      const fdt32_t *size_cells = fdt_getprop(fdt, child, "size", &property_len);
      int ranges_len = 0;
      const fdt32_t *ranges =
        fdt_getprop(fdt, child, "alloc-ranges", &ranges_len);
      if (!size_cells || property_len != 8 || !ranges || ranges_len < 16)
        panic("platform: unsupported dynamic reserved-memory node");
      reserve_range(read_cells(ranges, 2), read_cells(size_cells, 2));
    }
  }
  if (active.ram_end <= PGROUNDUP((uint64)end))
    panic("platform: no allocatable RAM after reservations");

  int uart = fdt_path_offset(fdt, "/soc/serial@10000000");
  if (uart < 0 || fdt_node_check_compatible(fdt, uart,
                                            "snps,dw-apb-uart") != 0)
    panic("platform: unsupported console UART");
  uint64 ignored = 0;
  active.uart_base = node_reg(fdt, uart, &ignored);
  active.uart_reg_shift = node_u32(fdt, uart, "reg-shift", 0);
  active.uart_reg_width = node_u32(fdt, uart, "reg-io-width", 1);
  active.uart_irq = node_u32(fdt, uart, "interrupts", 0);

  int plic = find_compatible(fdt, "riscv,plic0");
  active.plic_base = node_reg(fdt, plic, &ignored);

  int mmc = fdt_path_offset(fdt, "/soc/sdio1@16020000");
  if (mmc < 0 ||
      fdt_node_check_compatible(fdt, mmc, "starfive,jh7110-sdio") != 0)
    panic("platform: SD controller missing");
  active.block_base = node_reg(fdt, mmc, &ignored);
  active.block_irq = node_u32(fdt, mmc, "interrupts", 0);

  int cpus = fdt_path_offset(fdt, "/cpus");
  active.timebase_frequency = node_u32(fdt, cpus, "timebase-frequency", 0);
  if (active.timebase_frequency == 0)
    panic("platform: missing timebase-frequency");
  int cpu;
  fdt_for_each_subnode(cpu, fdt, cpus) {
    if (fdt_node_check_compatible(fdt, cpu, "sifive,u74-mc") != 0)
      continue;
    int len = 0;
    const fdt32_t *reg = fdt_getprop(fdt, cpu, "reg", &len);
    const char *status = fdt_getprop(fdt, cpu, "status", 0);
    if (!reg || len < 4 || (status && strncmp(status, "okay", 5) != 0))
      continue;
    if (active.hart_count >= NCPU)
      panic("platform: too many harts");
    active.hart_ids[active.hart_count++] = fdt32_to_cpu(*reg);
  }
  if (active.hart_count != 4)
    panic("platform: VisionFive 2 requires four U74 harts");

  long required[] = {SBI_EXT_TIME, SBI_EXT_IPI, SBI_EXT_RFENCE,
                     SBI_EXT_HSM, SBI_EXT_SRST};
  for (uint i = 0; i < sizeof(required) / sizeof(required[0]); i++)
    if (!sbi_probe_extension(required[i]))
      panic("platform: required SBI extension missing");
}
#endif

void
platform_early_init(uint64 hartid, uint64 dtb)
{
#ifdef PLATFORM_VISIONFIVE2
  parse_visionfive2(dtb);
  if (platform_cpu_index(hartid) != 0)
    panic("platform: firmware released an unordered secondary hart");
#else
  active = (struct platform_info){
    .name = "qemu-virt",
    .kernel_base = KERNBASE,
    .ram_base = KERNBASE,
    .ram_end = KERNBASE + 128 * 1024 * 1024,
    .uart_base = 0x10000000L,
    .plic_base = 0x0c000000L,
    .block_base = 0x10001000L,
    .uart_reg_shift = 0,
    .uart_reg_width = 1,
    .uart_irq = 10,
    .block_irq = 1,
    .timebase_frequency = 10000000,
    .hart_ids = {0, 1, 2},
    .hart_count = 3,
    .uses_sbi = 0,
    .block_kind = PLATFORM_BLOCK_VIRTIO,
  };
  (void)hartid;
  (void)dtb;
#endif
}

const struct platform_info *
platform_get(void)
{
  if (!active.name)
    panic("platform: used before initialization");
  return &active;
}

uint64
platform_hartid(int cpu_index)
{
  if (cpu_index < 0 || cpu_index >= active.hart_count)
    panic("platform: invalid CPU index");
  return active.hart_ids[cpu_index];
}

int
platform_cpu_index(uint64 hartid)
{
  for (int i = 0; i < active.hart_count; i++)
    if (active.hart_ids[i] == hartid)
      return i;
  return -1;
}

void
platform_start_harts(uint64 entry)
{
  if (!active.uses_sbi)
    return;
  for (int i = 1; i < active.hart_count; i++)
    if (sbi_hart_start(active.hart_ids[i], entry, i) != 0)
      panic("platform: SBI hart_start failed");
}

void
platform_set_timer(uint64 deadline)
{
  if (active.uses_sbi) {
    if (sbi_set_timer(deadline) != 0)
      platform_shutdown();
  } else {
    w_stimecmp(deadline);
  }
}

void
platform_shutdown(void)
{
  if (active.uses_sbi)
    sbi_system_reset(0, 0);
  for (;;)
    asm volatile("wfi");
}
