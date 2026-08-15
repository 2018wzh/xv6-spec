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
  printk("sd: reuse U-Boot-initialized controller\n");
  initlock(&sd_lock, "jh7110_sd");
  if ((*sdreg(SD_CDETECT) & 1) != 0)
    panic("sd: no card detected");
  // U-Boot has already reset the DesignWare controller, negotiated the card,
  // selected it with CMD7, and enabled the high-speed clock. Do not reset or
  // re-divide the clock here: the old CMD0/CMD8/ACMD41 sequence against the
  // JH7110 CIU does not observe CMD_DONE and hangs. Reuse the live transfer
  // state and only publish the FIFO/timeout values the PIO path needs.
  *sdreg(SD_INTMASK) = 0;
  *sdreg(SD_TMOUT) = 0xffffffff;
  *sdreg(SD_FIFOTH) = (4U << 28) | (15U << 16) | 16U;
  *sdreg(SD_RINTSTS) = 0xffffffff;
  printk("sd: controller state ready\n");
  partition_lba = gpt_find_xv6fs(read_sector);
  printk("sd: gpt ok, partition_lba=0x%lx\n", partition_lba);
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
#define INT_ERROR_MASK  (INT_ERROR & ~((1U<<11)|(1U<<9)|(1U<<5)|(1U<<4)|(1U<<3)|(1U<<2)))
#define STATUS_FIFO_EMPTY (1U << 2)
#define STATUS_FIFO_FULL  (1U << 3)
#define STATUS_DATA_BUSY  (1U << 9)

static struct spinlock sd_lock;
static uint32 card_rca __attribute__((unused));
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
  for (uint32 i = 0; i < 10000000; i++)
    if ((*sdreg(SD_CMD) & CMD_START) == 0)
      goto command_launched;
  printk("sd: launch timeout cpu=%d cmdreg=0x%x status=0x%x rint=0x%x\n",
         cpuid(), *sdreg(SD_CMD), *sdreg(SD_STATUS), *sdreg(SD_RINTSTS));
  panic("sd: command launch timeout");
command_launched:
  for (uint32 i = 0; i < 10000000; i++) {
    uint32 status = *sdreg(SD_RINTSTS);
    if (status & INT_ERROR)
      panic("sd: command error");
    if (status & INT_CMD_DONE) {
      // For data commands, leave CMD_DONE pending (as U-Boot's dw_mmc does):
      // clearing RINTSTS here can disturb the data phase before RXDR/DTO.
      if ((flags & CMD_DATA) == 0)
        *sdreg(SD_RINTSTS) = status;
      return *sdreg(SD_RESP0);
    }
  }
  panic("sd: command completion timeout");
}

static void
update_clock(void)
{
  *sdreg(SD_RINTSTS) = 0xffffffff;
  *sdreg(SD_CMD) = CMD_START | CMD_USE_HOLD | CMD_UPDATE_CLK | CMD_WAIT_DATA;
  for (uint32 i = 0; i < 10000000; i++)
    if ((*sdreg(SD_CMD) & CMD_START) == 0)
      return;
  panic("sd: clock update timeout");
}

static void
__attribute__((unused)) set_clock(uint32 divisor)
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
fifo_reset(void)
{
  *sdreg(SD_CTRL) |= (1U << 1); // DWMCI_CTRL_FIFO_RESET
  wait_clear(SD_CTRL, (1U << 1), "sd: fifo reset timeout");
}

static void
read_sector(uint64 lba, uchar *data)
{
  if (lba > 0xffffffffU)
    panic("sd: sector outside SDHC range");
  *sdreg(SD_BLKSIZ) = 512;
  *sdreg(SD_BYTCNT) = 512;
  fifo_reset();
  uint32 r17 = command(17, (uint32)lba,
          CMD_RESP | CMD_RESP_CRC | CMD_DATA | CMD_WAIT_DATA);
  if (lba == 0)
    printk("sd: cmd17 resp=0x%x status=0x%x fifo=0x%x\n", r17,
           *sdreg(SD_RINTSTS), *sdreg(SD_STATUS));
  uint words = 0;
  for (uint32 timeout = 0; words < 128 && timeout < 10000000; timeout++) {
    uint32 status = *sdreg(SD_RINTSTS);
    if (status & INT_ERROR_MASK)
      panic("sd: read error");
    while (words < 128 && (*sdreg(SD_STATUS) & STATUS_FIFO_EMPTY) == 0)
      ((uint32 *)data)[words++] = *sdreg(SD_DATA);
    if ((status & INT_DATA_OVER) && words == 128)
      break;
  }
  if (words != 128)
    panic("sd: data timeout");
  *sdreg(SD_RINTSTS) = 0xffffffff;
  if (lba == 0)
    printk("sd: sector0 %x %x %x %x ... %x %x %x %x | b0=%x b1=%x b510=%x b511=%x\n",
           ((uint32 *)data)[0], ((uint32 *)data)[1], ((uint32 *)data)[2],
           ((uint32 *)data)[3], ((uint32 *)data)[60], ((uint32 *)data)[61],
           ((uint32 *)data)[62], ((uint32 *)data)[63], data[0], data[1],
           data[510], data[511]);
}
static void
write_sector(uint64 lba, const uchar *data)
{
  if (lba > 0xffffffffU)
    panic("sd: sector outside SDHC range");
  *sdreg(SD_BLKSIZ) = 512;
  *sdreg(SD_BYTCNT) = 512;
  fifo_reset();
  command(24, (uint32)lba,
          CMD_RESP | CMD_RESP_CRC | CMD_DATA | CMD_WRITE | CMD_WAIT_DATA);
  uint words = 0;
  for (uint32 timeout = 0; words < 128 && timeout < 10000000; timeout++) {
    uint32 status = *sdreg(SD_RINTSTS);
    if (status & INT_ERROR_MASK)
      panic("sd: write error");
    if (status & INT_TXDR) {
      while (words < 128 && (*sdreg(SD_STATUS) & STATUS_FIFO_FULL) == 0)
        *sdreg(SD_DATA) = ((const uint32 *)data)[words++];
      *sdreg(SD_RINTSTS) = status & INT_TXDR;
    }
    if ((status & INT_DATA_OVER) && words == 128)
      break;
  }
  if (words != 128)
    panic("sd: write data timeout");
  wait_clear(SD_STATUS, STATUS_DATA_BUSY, "sd: write completion timeout");
  *sdreg(SD_RINTSTS) = 0xffffffff;
}

extern uint64 gpt_find_xv6fs(void (*read)(uint64, uchar *));

void
jh7110_sd_init(void)
{
  printk("sd: init start\n");
  initlock(&sd_lock, "jh7110_sd");
  if ((*sdreg(SD_CDETECT) & 1) != 0)
    panic("sd: no card detected");
  *sdreg(SD_PWREN) = 1;
  *sdreg(SD_CTRL) = CTRL_RESET_ALL;
  wait_clear(SD_CTRL, CTRL_RESET_ALL, "sd: controller reset timeout");
  *sdreg(SD_BMOD) = 0;
  *sdreg(SD_INTMASK) = 0;
  *sdreg(SD_TMOUT) = 0xffffffff;
  *sdreg(SD_FIFOTH) = (4U << 28) | (15U << 16) | 1U;
  *sdreg(SD_UHS_REG) = 0;
  printk("sd: reset ok\n");
  set_clock(124);
  printk("sd: low clock ok\n");

  command(0, 0, CMD_SEND_INIT);
  command(8, 0x1aa, CMD_RESP | CMD_RESP_CRC);
  printk("sd: cmd0/cmd8 ok\n");
  uint32 ocr = 0;
  for (uint32 retry = 0; retry < 10000; retry++) {
    command(55, 0, CMD_RESP | CMD_RESP_CRC);
    ocr = command(41, 0x40300000, CMD_RESP);
    if (ocr & (1U << 31))
      break;
  }
  if ((ocr & (1U << 31)) == 0 || (ocr & (1U << 30)) == 0)
    panic("sd: SDHC initialization failed");
  printk("sd: acmd41 ok\n");
  command(2, 0, CMD_RESP | CMD_RESP_LONG | CMD_RESP_CRC);
  uint32 rca_resp = command(3, 0, CMD_RESP | CMD_RESP_CRC);
  card_rca = rca_resp & 0xffff0000;
  printk("sd: rca resp=0x%x card_rca=0x%x\n", rca_resp, card_rca);
  if (card_rca == 0)
    panic("sd: invalid RCA");
  command(7, card_rca, CMD_RESP | CMD_RESP_CRC);
  command(55, card_rca, CMD_RESP | CMD_RESP_CRC);
  command(6, 2, CMD_RESP | CMD_RESP_CRC); // 4-bit bus, matches U-Boot
  printk("sd: card select ok (1-bit)\n");
  *sdreg(SD_CTYPE) = 1;
  set_clock(4);
  printk("sd: high clock ok\n");
  partition_lba = gpt_find_xv6fs(read_sector);
  printk("sd: gpt ok, partition_lba=0x%lx\n", partition_lba);
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
