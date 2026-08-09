#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "defs.h"
#include "platform.h"

void
disk_init(void)
{
  switch (platform_get()->block_kind) {
  case PLATFORM_BLOCK_VIRTIO:
    virtio_disk_init();
    return;
  case PLATFORM_BLOCK_JH7110_SD:
    jh7110_sd_init();
    return;
  }
  panic("blockdev: unsupported backend");
}

void
disk_rw(struct buf *b, int write)
{
  switch (platform_get()->block_kind) {
  case PLATFORM_BLOCK_VIRTIO:
    virtio_disk_rw(b, write);
    return;
  case PLATFORM_BLOCK_JH7110_SD:
    jh7110_sd_rw(b, write);
    return;
  }
  panic("blockdev: unsupported backend");
}

void
disk_intr(void)
{
  switch (platform_get()->block_kind) {
  case PLATFORM_BLOCK_VIRTIO:
    virtio_disk_intr();
    return;
  case PLATFORM_BLOCK_JH7110_SD:
    jh7110_sd_intr();
    return;
  }
  panic("blockdev: unsupported backend");
}
