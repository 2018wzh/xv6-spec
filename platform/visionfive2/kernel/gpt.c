#include "types.h"
#include "riscv.h"
#include "defs.h"

#define SECTOR_SIZE 512
#define GPT_HEADER_SIZE_MIN 92
#define GPT_ENTRY_SIZE 128
#define GPT_MAX_ENTRIES 128

struct gpt_header {
  uchar signature[8];
  uint32 revision;
  uint32 header_size;
  uint32 header_crc32;
  uint32 reserved;
  uint64 current_lba;
  uint64 backup_lba;
  uint64 first_usable_lba;
  uint64 last_usable_lba;
  uchar disk_guid[16];
  uint64 entries_lba;
  uint32 entry_count;
  uint32 entry_size;
  uint32 entries_crc32;
} __attribute__((packed));

struct gpt_entry {
  uchar type_guid[16];
  uchar unique_guid[16];
  uint64 first_lba;
  uint64 last_lba;
  uint64 attributes;
  uint16 name[36];
} __attribute__((packed));

static uint32
crc32_update(uint32 crc, const uchar *data, uint length)
{
  crc = ~crc;
  for (uint i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

static int
is_xv6fs(const struct gpt_entry *entry)
{
  // Linux filesystem-data GUID 0FC63DAF-8483-4772-8E79-3D69D8477DE4,
  // represented in GPT's mixed-endian on-disk encoding.
  static const uchar type[16] = {
    0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
    0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4,
  };
  static const char name[] = "xv6fs";
  if (memcmp(entry->type_guid, type, sizeof(type)) != 0)
    return 0;
  for (uint i = 0; i < sizeof(name) - 1; i++)
    if (entry->name[i] != (uint16)name[i])
      return 0;
  return entry->name[sizeof(name) - 1] == 0;
}

uint64
gpt_find_xv6fs(void (*read_sector)(uint64, uchar *))
{
  uchar sector[SECTOR_SIZE];
  read_sector(0, sector);
  if (sector[510] != 0x55 || sector[511] != 0xaa || sector[450] != 0xee)
    panic("gpt: protective MBR missing");

  read_sector(1, sector);
  struct gpt_header header;
  memmove(&header, sector, sizeof(header));
  static const uchar signature[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
  if (memcmp(header.signature, signature, sizeof(signature)) != 0 ||
      header.revision != 0x00010000 ||
      header.header_size < GPT_HEADER_SIZE_MIN ||
      header.header_size > SECTOR_SIZE || header.current_lba != 1 ||
      header.entry_count == 0 || header.entry_count > GPT_MAX_ENTRIES ||
      header.entry_size != GPT_ENTRY_SIZE || header.entries_lba < 2)
    panic("gpt: invalid primary header");

  uint32 expected_header_crc = header.header_crc32;
  ((struct gpt_header *)sector)->header_crc32 = 0;
  if (crc32_update(0, sector, header.header_size) != expected_header_crc)
    panic("gpt: header checksum mismatch");

  uint32 entries_crc = 0;
  uint64 match_lba = 0;
  uint entries_per_sector = SECTOR_SIZE / GPT_ENTRY_SIZE;
  for (uint index = 0; index < header.entry_count;) {
    read_sector(header.entries_lba + index / entries_per_sector, sector);
    uint remaining = header.entry_count - index;
    uint count = remaining < entries_per_sector ? remaining : entries_per_sector;
    entries_crc = crc32_update(entries_crc, sector, count * GPT_ENTRY_SIZE);
    for (uint slot = 0; slot < count; slot++, index++) {
      const struct gpt_entry *entry =
        (const struct gpt_entry *)(sector + slot * GPT_ENTRY_SIZE);
      if (!is_xv6fs(entry))
        continue;
      if (match_lba != 0)
        panic("gpt: duplicate xv6fs partition");
      if (entry->first_lba < header.first_usable_lba ||
          entry->last_lba > header.last_usable_lba ||
          entry->first_lba > entry->last_lba)
        panic("gpt: xv6fs outside usable range");
      match_lba = entry->first_lba;
    }
  }
  if (entries_crc != header.entries_crc32)
    panic("gpt: partition checksum mismatch");
  if (match_lba == 0)
    panic("gpt: xv6fs partition missing");
  return match_lba;
}
