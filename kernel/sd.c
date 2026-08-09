#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "defs.h"

// The JH7110 backend is selected only by the VisionFive 2 build. Keeping the
// unsupported path explicit prevents a QEMU image from silently treating a
// different controller as virtio.
#ifndef PLATFORM_VISIONFIVE2
void
jh7110_sd_init(void)
{
  panic("sd: backend unavailable on qemu-virt");
}

void
jh7110_sd_rw(struct buf *b, int write)
{
  (void)b;
  (void)write;
  panic("sd: backend unavailable on qemu-virt");
}

void
jh7110_sd_intr(void)
{
  panic("sd: unexpected interrupt on qemu-virt");
}
#else
#include "platform.h"

// Synopsys DesignWare Mobile Storage Host Controller registers used by the
// starfive,jh7110-mmc binding. This driver intentionally uses polling during
// early bring-up; every timeout and card error is fatal and observable.
#define SD_CTRL       0x000
#define SD_PWREN      0x004
#define SD_CLKDIV     0x008
#define SD_CLKSRC     0x00c
#define SD_CLKENA     0x010
#define SD_TMOUT      0x014
#define SD_CTYPE      0x018
#define SD_BLKSIZ     0x01c
#define SD_BYTCNT     0x020
#define SD_INTMASK    0x024
#define SD_CMDARG     0x028
#define SD_CMD        0x02c
#define SD_RESP0      0x030
#define SD_RINTSTS    0x044
#define SD_STATUS     0x048
#define SD_FIFOTH     0x04c
#define SD_CDETECT    0x050
#define SD_UHS_REG    0x074
#define SD_BMOD       0x080
#define SD_DATA       0x200

#define CTRL_RESET_ALL 0x7
#define CMD_START       (1U << 31)
#define CMD_USE_HOLD    (1U << 29)
#define CMD_UPDATE_CLK  (1U << 21)
#define CMD_SEND_INIT   (1U << 15)
#define CMD_STOP_ABORT  (1U << 14)
#define CMD_WAIT_DATA   (1U << 13)
#define CMD_WRITE       (1U << 10)
#define CMD_DATA        (1U << 9)
#define CMD_RESP_CRC    (1U << 8)
#define CMD_RESP_LONG   (1U << 7)
#define CMD_RESP        (1U << 6)

#define INT_CMD_DONE    (1U << 2)
#define INT_DATA_OVER   (1U << 3)
#define INT_TXDR        (1U << 4)
#define INT_RXDR        (1U << 5)
#define INT_ERROR       0xbfc2U
#define STATUS_FIFO_EMPTY (1U << 2)
#define STATUS_FIFO_FULL  (1U << 3)
#define STATUS_DATA_BUSY  (1U << 9)

static struct spinlock sd_lock;
static uint32 card_rca;
static uint64 partition_lba;

static volatile uint32 *
sdreg(uint32 offset)
{
  return (volatile uint32 *)(platform_get()->block_base + offset);
}

static void
wait_clear(uint32 offset, uint32 mask, char *message)
{
  for (uint32 i = 0; i < 10000000; i++)
    if ((*sdreg(offset) & mask) == 0)
      return;
  panic(message);
}

static uint32
command(uint32 index, uint32 argument, uint32 flags)
{
  wait_clear(SD_STATUS, STATUS_DATA_BUSY, "sd: data busy timeout");
  *sdreg(SD_RINTSTS) = 0xffffffff;
  *sdreg(SD_CMDARG) = argument;
  *sdreg(SD_CMD) = CMD_START | CMD_USE_HOLD | flags | index;
  wait_clear(SD_CMD, CMD_START, "sd: command launch timeout");
  for (uint32 i = 0; i < 10000000; i++) {
    uint32 status = *sdreg(SD_RINTSTS);
    if (status & INT_ERROR)
      panic("sd: command error");
    if (status & INT_CMD_DONE) {
      *sdreg(SD_RINTSTS) = status;
      return *sdreg(SD_RESP0);
    }
  }
  panic("sd: command completion timeout");
}

static void
update_clock(void)
{
  command(0, 0, CMD_UPDATE_CLK | CMD_WAIT_DATA);
}

static void
set_clock(uint32 divisor)
{
  *sdreg(SD_CLKENA) = 0;
  update_clock();
  *sdreg(SD_CLKSRC) = 0;
  *sdreg(SD_CLKDIV) = divisor;
  update_clock();
  *sdreg(SD_CLKENA) = 1;
  update_clock();
}

static void
read_sector(uint64 lba, uchar *data)
{
  if (lba > 0xffffffffU)
    panic("sd: sector outside SDHC range");
  *sdreg(SD_BLKSIZ) = 512;
  *sdreg(SD_BYTCNT) = 512;
  command(17, (uint32)lba,
          CMD_RESP | CMD_RESP_CRC | CMD_DATA | CMD_WAIT_DATA);
  uint words = 0;
  while (words < 128) {
    uint32 status = *sdreg(SD_RINTSTS);
    if (status & INT_ERROR)
      panic("sd: read error");
    while (words < 128 && (*sdreg(SD_STATUS) & STATUS_FIFO_EMPTY) == 0)
      ((uint32 *)data)[words++] = *sdreg(SD_DATA);
    if ((status & INT_DATA_OVER) && words == 128)
      break;
  }
  *sdreg(SD_RINTSTS) = 0xffffffff;
}

static void
write_sector(uint64 lba, const uchar *data)
{
  if (lba > 0xffffffffU)
    panic("sd: sector outside SDHC range");
  *sdreg(SD_BLKSIZ) = 512;
  *sdreg(SD_BYTCNT) = 512;
  command(24, (uint32)lba,
          CMD_RESP | CMD_RESP_CRC | CMD_DATA | CMD_WRITE | CMD_WAIT_DATA);
  uint words = 0;
  while (words < 128) {
    uint32 status = *sdreg(SD_RINTSTS);
    if (status & INT_ERROR)
      panic("sd: write error");
    while (words < 128 && (*sdreg(SD_STATUS) & STATUS_FIFO_FULL) == 0)
      *sdreg(SD_DATA) = ((const uint32 *)data)[words++];
    if ((status & INT_DATA_OVER) && words == 128)
      break;
  }
  wait_clear(SD_STATUS, STATUS_DATA_BUSY, "sd: write completion timeout");
  *sdreg(SD_RINTSTS) = 0xffffffff;
}

extern uint64 gpt_find_xv6fs(void (*read)(uint64, uchar *));

void
jh7110_sd_init(void)
{
  initlock(&sd_lock, "jh7110_sd");
  if ((*sdreg(SD_CDETECT) & 1) != 0)
    panic("sd: no card detected");
  *sdreg(SD_PWREN) = 1;
  *sdreg(SD_CTRL) = CTRL_RESET_ALL;
  wait_clear(SD_CTRL, CTRL_RESET_ALL, "sd: controller reset timeout");
  *sdreg(SD_BMOD) = 1;
  *sdreg(SD_INTMASK) = 0;
  *sdreg(SD_TMOUT) = 0xffffffff;
  *sdreg(SD_FIFOTH) = (4U << 28) | (15U << 16) | 16U;
  *sdreg(SD_UHS_REG) = 0;
  set_clock(124); // approximately 400 kHz from the 100 MHz CIU clock

  command(0, 0, CMD_SEND_INIT);
  command(8, 0x1aa, CMD_RESP | CMD_RESP_CRC);
  uint32 ocr = 0;
  for (uint32 retry = 0; retry < 10000; retry++) {
    command(55, 0, CMD_RESP | CMD_RESP_CRC);
    ocr = command(41, 0x40300000, CMD_RESP);
    if (ocr & (1U << 31))
      break;
  }
  if ((ocr & (1U << 31)) == 0 || (ocr & (1U << 30)) == 0)
    panic("sd: SDHC initialization failed");
  command(2, 0, CMD_RESP | CMD_RESP_LONG | CMD_RESP_CRC);
  card_rca = command(3, 0, CMD_RESP | CMD_RESP_CRC) & 0xffff0000;
  if (card_rca == 0)
    panic("sd: invalid RCA");
  command(7, card_rca, CMD_RESP | CMD_RESP_CRC);
  command(55, card_rca, CMD_RESP | CMD_RESP_CRC);
  command(6, 2, CMD_RESP | CMD_RESP_CRC);
  *sdreg(SD_CTYPE) = 1;
  set_clock(1);
  partition_lba = gpt_find_xv6fs(read_sector);
}

void
jh7110_sd_rw(struct buf *b, int write)
{
  acquire(&sd_lock);
  uint64 sector = partition_lba + (uint64)b->blockno * 2;
  for (uint i = 0; i < BSIZE / 512; i++) {
    if (write)
      write_sector(sector + i, b->data + i * 512);
    else
      read_sector(sector + i, b->data + i * 512);
  }
  release(&sd_lock);
}

void
jh7110_sd_intr(void)
{
  uint32 status = *sdreg(SD_RINTSTS);
  *sdreg(SD_RINTSTS) = status;
  if (status & INT_ERROR)
    panic("sd: interrupt error");
}
#endif
