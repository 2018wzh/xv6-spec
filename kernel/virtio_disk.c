// virtio_disk.c - The xv6 legacy virtio block driver for the QEMU virt
// board's first virtio-mmio disk interface (VIRTIO0).
//
// This module owns the virtio block queue, descriptor lifetime, interrupt
// completion, and the conversion between 1024-byte xv6 logical blocks and
// 512-byte device sectors. Every descriptor is either free or belongs to
// exactly one in-flight request, and logical block n always maps to the two
// adjacent 512-byte sectors 2*n and 2*n+1 so adjacent blocks neither overlap
// nor leave gaps.
//
// A single legacy virtqueue with fixed three-descriptor chains per request is
// used: one out descriptor (the request header), one data descriptor (1024
// bytes), and one in descriptor (a completion status byte). virtio_disk_rw
// submits a chain and sleeps on its bookkeeping entry until the interrupt
// handler (virtio_disk_intr) reclaims it and wakes the waiter.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"

// Access virtio MMIO register r as a 32-bit register at VIRTIO0.
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

// Virtio legacy MMIO register offsets (QEMU virt transport).
#define VIRTIO_MMIO_MAGIC_VALUE      0x00
#define VIRTIO_MMIO_VERSION          0x04
#define VIRTIO_MMIO_DEVICE_ID        0x08
#define VIRTIO_MMIO_VENDOR_ID        0x0c
#define VIRTIO_MMIO_DEVICE_FEATURES  0x10
#define VIRTIO_MMIO_DRIVER_FEATURES  0x20
#define VIRTIO_MMIO_QUEUE_SEL        0x30
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x34
#define VIRTIO_MMIO_QUEUE_NUM        0x38
#define VIRTIO_MMIO_QUEUE_PFN        0x40
#define VIRTIO_MMIO_QUEUE_READY      0x44
#define VIRTIO_MMIO_QUEUE_NOTIFY     0x50
#define VIRTIO_MMIO_INTR_STATUS      0x60
#define VIRTIO_MMIO_STATUS           0x70

// Driver status bits written to VIRTIO_MMIO_STATUS during negotiation.
#define VIRTIO_CONFIG_S_ACKNOWLEDGE  1
#define VIRTIO_CONFIG_S_DRIVER       2
#define VIRTIO_CONFIG_S_DRIVER_OK    4
#define VIRTIO_CONFIG_S_FEATURES_OK  8

// Block request type field values.
#define VIRTIO_BLK_T_IN  0   // read
#define VIRTIO_BLK_T_OUT 1   // write

// Feature bits we deliberately do not negotiate.
#define VIRTIO_BLK_F_RO     5  // read-only disk
#define VIRTIO_BLK_F_FLUSH  9  // flush support

// Number of descriptors in the single queue.
#define NUM 8

// Descriptor flag bits.
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

// A virtio descriptor: a scatter-gather entry in the descriptor ring.
struct VRingDesc {
  uint64 addr;   // physical address of the buffer
  uint32 len;    // length of the buffer
  uint16 flags;  // VRING_DESC_F_* bits
  uint16 next;   // index of the next descriptor in the chain
};

// The available ring: buffers offered to the device.
struct VRingAvail {
  uint16 flags;
  uint16 idx;             // total number of buffers ever offered
  uint16 ring[NUM];       // descriptor chain head indices
  uint16 unused;
};

// The used ring: buffers returned by the device after completion.
struct UsedArea {
  uint16 flags;
  uint16 idx;             // total number of buffers ever returned
  struct {
    uint32 id;            // index of the descriptor chain head
    uint32 len;           // bytes written by the device
  } elems[NUM];
};

// The request header placed in the first descriptor of each chain.
struct virtio_blk_outhdr {
  uint32 type;      // VIRTIO_BLK_T_IN or VIRTIO_BLK_T_OUT
  uint32 reserved;
  uint64 sector;    // 512-byte device sector
};

// Per-descriptor-chain bookkeeping used to match a completion to its waiter.
struct virtio_blk_info {
  void *data;   // the 1024-byte logical block buffer
  char status;  // 0xff = in flight; 0 = completed successfully
};

// The single disk/queue state. Queue memory lives in the contiguous
// `pages` array (identity-mapped so its kernel virtual address equals the
// physical address handed to the legacy QUEUE_PFN register).
struct disk {
  char pages[3 * PGSIZE];
  struct VRingDesc *desc;
  uint16 *avail;
  struct UsedArea *used;

  char free[NUM];                  // is descriptor i free?
  struct virtio_blk_info info[NUM];// per-chain bookkeeping

  uint16 used_idx;                 // how far into used we have consumed
};

static struct disk disk;
static struct spinlock disk_lock;

// Mark descriptor i free and wake any waiter blocked on descriptor pressure.
static void
free_desc(int i)
{
  if (i >= NUM)
    panic("free_desc 1");
  if (disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}

// Return one free descriptor, or -1 when the fixed pool is exhausted.
static int
alloc_desc(void)
{
  int i;
  for (i = 0; i < NUM; i++) {
    if (disk.free[i]) {
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// Free a whole descriptor chain starting at head i.
static void
free_chain(int i)
{
  for (;;) {
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if (flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// Allocate three descriptors for one chain, or return -1 (releasing any
// partial allocation) when the pool cannot satisfy the request.
static int
alloc3_desc(int *idx)
{
  int i;
  for (i = 0; i < 3; i++) {
    idx[i] = alloc_desc();
    if (idx[i] < 0) {
      int j;
      for (j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

// Initialize the virtio block device and its single legacy queue. Validates
// device identity, negotiates features, and prepares the descriptor,
// available, and used rings before marking the queue ready. Runs once on the
// boot hart before any disk traffic. Unsupported identity or queue geometry
// panics with a diagnostic.
void
virtio_disk_init(void)
{
  uint32 status = 0;
  uint64 features;
  uint32 max;

  initlock(&disk_lock, "virtio_disk");

  // Validate device identity: magic "virt", legacy version 1, block device
  // id 2, and the QEMU vendor id.
  if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
      *R(VIRTIO_MMIO_VERSION) != 1 ||
      *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
      *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    panic("could not find virtio disk");
  }

  // Acknowledge the device.
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // Announce the driver.
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // Negotiate features: accept everything except read-only and flush.
  features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_FLUSH);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // Select queue 0 and validate its geometry.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;
  max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0)
    panic("virtio disk has no queue 0");
  if (max < NUM)
    panic("virtio disk max queue too short");

  // Lay out the contiguous queue pages: descriptors, available ring, used ring.
  disk.desc = (struct VRingDesc *)disk.pages;
  disk.avail = (uint16 *)(disk.pages + PGSIZE);
  disk.used = (struct UsedArea *)(disk.pages + 2 * PGSIZE);

  // Initialize every descriptor as free and zero the queue memory.
  {
    int i;
    for (i = 0; i < NUM; i++)
      disk.free[i] = 1;
    for (i = 0; i < 3 * PGSIZE / sizeof(char); i++)
      disk.pages[i] = 0;
  }
  disk.used_idx = 0;

  // Tell the device the physical page frame of the queue. Kernel virtual
  // addresses equal physical addresses here (identity-mapped kernel image).
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
  *R(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)disk.pages) >> 12;
  *R(VIRTIO_MMIO_QUEUE_READY) = 1;
}

// Transfer one 1024-byte logical block between the device and `data`.
// `blockno` is the logical block number; it maps to the two adjacent
// 512-byte sectors 2*blockno and 2*blockno+1. `write` selects a device write
// (data is sent to the disk) versus a read (the disk fills data). The caller
// must hold the target buffer's sleep lock and supply a valid block number.
// This is synchronous: it returns only after both sectors of the logical
// block have completed, or panics on a malformed completion.
void
virtio_disk_rw(uint64 blockno, void *data, int write)
{
  uint64 sector0 = blockno * 2;
  int idx[3];
  struct virtio_blk_outhdr hdr;
  struct virtio_blk_info *info;

  if ((blockno & 0x1fffffffffffffffULL) == 0x1fffffffffffffffULL)
    panic("virtio_disk_rw: sector overflow");

  acquire(&disk_lock);

  // Allocate a three-descriptor chain, waiting (without leaking a partial
  // chain) when the fixed pool is momentarily exhausted.
  while (alloc3_desc(idx) < 0)
    sleep(&disk.free[0], &disk_lock);

  info = &disk.info[idx[0]];

  // Descriptor 0: the request header (out).
  hdr.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  hdr.reserved = 0;
  hdr.sector = sector0;
  disk.desc[idx[0]].addr = (uint64)&hdr;
  disk.desc[idx[0]].len = sizeof(hdr);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  // Descriptor 1: the 1024-byte data buffer.
  disk.desc[idx[1]].addr = (uint64)data;
  disk.desc[idx[1]].len = 1024;
  if (write)
    disk.desc[idx[1]].flags = 0;                  // device reads from it
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes into it
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  // Descriptor 2: the completion status byte (in).
  info->status = 0xff;
  disk.desc[idx[2]].addr = (uint64)&info->status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  disk.desc[idx[2]].next = 0;

  // Record the data buffer so the interrupt handler can wake this waiter.
  info->data = data;

  // Publish the chain head in the available ring.
  disk.avail[2 + disk.avail[1]] = idx[0];
  __sync_synchronize();
  disk.avail[1] = disk.avail[1] + 1;
  __sync_synchronize();

  // Kick the device (queue number 0).
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  // Wait until virtio_disk_intr reclaims the chain and clears status.
  while (info->status != 0)
    sleep(info, &disk_lock);

  if (info->status != 0)
    panic("virtio_disk_rw: device reported failure");

  free_chain(idx[0]);

  release(&disk_lock);
}

// Reclaim completed descriptor chains from the used ring and wake each
// waiter exactly once. Invoked after PLIC claim reports the virtio IRQ.
// A malformed used-ring descriptor or completion corruption panics.
void
virtio_disk_intr(void)
{
  uint32 used_idx;
  uint32 i;

  acquire(&disk_lock);

  // Acknowledge the device interrupt so it may raise the next one.
  *R(VIRTIO_MMIO_INTR_STATUS) = *R(VIRTIO_MMIO_INTR_STATUS);

  used_idx = disk.used->idx;

  // Consume every newly returned chain.
  for (i = 0; i < used_idx - disk.used_idx; i++) {
    uint32 id = disk.used->elems[(disk.used_idx + i) % NUM].id;
    struct virtio_blk_info *info;

    if (id >= NUM)
      panic("virtio_disk_intr: used id");
    info = &disk.info[id];
    if (info->status != 0xff)
      panic("virtio_disk_intr: status");
    info->status = 0;
    wakeup(info);
  }
  disk.used_idx = used_idx;

  release(&disk_lock);
}
