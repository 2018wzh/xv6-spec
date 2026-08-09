#!/usr/bin/env python3
"""Build and inspect the deterministic GPT/FAT VisionFive 2 SD image."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import pathlib
import struct
import uuid

SECTOR = 512
TOTAL_SECTORS = 256 * 1024 * 1024 // SECTOR
BOOT_FIRST = 2048
BOOT_SECTORS = 64 * 1024 * 1024 // SECTOR
FS_FIRST = BOOT_FIRST + BOOT_SECTORS
FS_LAST = TOTAL_SECTORS - 34
ENTRY_COUNT = 128
ENTRY_SIZE = 128
ENTRIES_LBA = 2
EFI_GUID = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
LINUX_FS_GUID = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4")
NAMESPACE = uuid.UUID("469c8097-9e0a-4e3a-9f8a-835b705e1b75")


def utf16_name(name: str) -> bytes:
    raw = name.encode("utf-16le")
    if len(raw) > 72:
        raise ValueError("GPT partition name too long")
    return raw.ljust(72, b"\0")


def entry(kind: uuid.UUID, unique: uuid.UUID, first: int, last: int, name: str) -> bytes:
    return struct.pack("<16s16sQQQ72s", kind.bytes_le, unique.bytes_le,
                       first, last, 0, utf16_name(name))


def header(current: int, backup: int, entries_lba: int, entries_crc: int,
           disk_guid: uuid.UUID) -> bytes:
    raw = struct.pack("<8sIIIIQQQQ16sQIII", b"EFI PART", 0x10000, 92, 0, 0,
                      current, backup, 34, TOTAL_SECTORS - 34,
                      disk_guid.bytes_le, entries_lba, ENTRY_COUNT,
                      ENTRY_SIZE, entries_crc)
    crc = binascii.crc32(raw) & 0xFFFFFFFF
    return (raw[:16] + struct.pack("<I", crc) + raw[20:]).ljust(SECTOR, b"\0")


def make_fat16(payload: bytes) -> bytes:
    sectors_per_cluster = 4
    reserved = 1
    fats = 2
    root_entries = 512
    root_sectors = root_entries * 32 // SECTOR
    fat_sectors = 129
    data_start = reserved + fats * fat_sectors + root_sectors
    clusters = (len(payload) + sectors_per_cluster * SECTOR - 1) // (
        sectors_per_cluster * SECTOR)
    if clusters < 1 or clusters + 2 >= fat_sectors * SECTOR // 2:
        raise ValueError("FIT does not fit the FAT16 boot partition")
    image = bytearray(BOOT_SECTORS * SECTOR)
    boot = bytearray(SECTOR)
    boot[:3] = b"\xeb\x3c\x90"
    boot[3:11] = b"XV6SPEC "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, sectors_per_cluster,
                     reserved, fats, root_entries, 0, 0xF8,
                     fat_sectors, 32, 64, 0, BOOT_SECTORS)
    boot[36] = 0x80
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x58563632)
    boot[43:54] = b"XV6 BOOT   "
    boot[54:62] = b"FAT16   "
    boot[510:512] = b"\x55\xaa"
    image[:SECTOR] = boot
    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
    for cluster in range(2, 2 + clusters):
        next_cluster = 0xFFFF if cluster == clusters + 1 else cluster + 1
        struct.pack_into("<H", fat, cluster * 2, next_cluster)
    for index in range(fats):
        offset = (reserved + index * fat_sectors) * SECTOR
        image[offset:offset + len(fat)] = fat
    root_offset = (reserved + fats * fat_sectors) * SECTOR
    root = struct.pack("<11sBBBHHHHHHHI", b"XV6     ITB", 0x20, 0, 0,
                       0, 0, 0, 0, 0, 0, 2, len(payload))
    image[root_offset:root_offset + 32] = root
    payload_offset = data_start * SECTOR
    image[payload_offset:payload_offset + len(payload)] = payload
    return bytes(image)


def build(output: pathlib.Path, fit: pathlib.Path, fs: pathlib.Path) -> None:
    fit_data = fit.read_bytes()
    fs_data = fs.read_bytes()
    if len(fs_data) > (FS_LAST - FS_FIRST + 1) * SECTOR:
        raise SystemExit("fs.img exceeds the xv6fs partition")
    disk_guid = uuid.uuid5(NAMESPACE, "xv6-spec-vf2-disk")
    entries = bytearray(ENTRY_COUNT * ENTRY_SIZE)
    entries[:ENTRY_SIZE] = entry(EFI_GUID, uuid.uuid5(NAMESPACE, "boot"),
                                 BOOT_FIRST, BOOT_FIRST + BOOT_SECTORS - 1,
                                 "xv6boot")
    entries[ENTRY_SIZE:2 * ENTRY_SIZE] = entry(
        LINUX_FS_GUID, uuid.uuid5(NAMESPACE, "xv6fs"), FS_FIRST, FS_LAST, "xv6fs")
    entries_crc = binascii.crc32(entries) & 0xFFFFFFFF
    image = bytearray(TOTAL_SECTORS * SECTOR)
    image[446:462] = struct.pack("<B3sB3sII", 0, b"\0\x02\0", 0xEE,
                                 b"\xff\xff\xff", 1, min(TOTAL_SECTORS - 1, 0xFFFFFFFF))
    image[510:512] = b"\x55\xaa"
    image[SECTOR:2 * SECTOR] = header(1, TOTAL_SECTORS - 1, ENTRIES_LBA,
                                     entries_crc, disk_guid)
    image[ENTRIES_LBA * SECTOR:(ENTRIES_LBA * SECTOR) + len(entries)] = entries
    backup_entries_lba = TOTAL_SECTORS - 33
    start = backup_entries_lba * SECTOR
    image[start:start + len(entries)] = entries
    image[(TOTAL_SECTORS - 1) * SECTOR:] = header(
        TOTAL_SECTORS - 1, 1, backup_entries_lba, entries_crc, disk_guid)
    boot = make_fat16(fit_data)
    image[BOOT_FIRST * SECTOR:(BOOT_FIRST + BOOT_SECTORS) * SECTOR] = boot
    image[FS_FIRST * SECTOR:FS_FIRST * SECTOR + len(fs_data)] = fs_data
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)


def inspect(image_path: pathlib.Path, expected_fit: pathlib.Path | None) -> None:
    data = image_path.read_bytes()
    if len(data) != TOTAL_SECTORS * SECTOR or data[510:512] != b"\x55\xaa":
        raise SystemExit("invalid SD image size or protective MBR")
    primary = data[SECTOR:2 * SECTOR]
    if primary[:8] != b"EFI PART":
        raise SystemExit("primary GPT signature missing")
    header_size, expected_crc = struct.unpack_from("<II", primary, 12)
    checked = bytearray(primary[:header_size])
    checked[16:20] = b"\0" * 4
    if binascii.crc32(checked) & 0xFFFFFFFF != expected_crc:
        raise SystemExit("primary GPT checksum mismatch")
    entries = data[ENTRIES_LBA * SECTOR:(ENTRIES_LBA * SECTOR) + ENTRY_COUNT * ENTRY_SIZE]
    entries_crc = struct.unpack_from("<I", primary, 88)[0]
    if binascii.crc32(entries) & 0xFFFFFFFF != entries_crc:
        raise SystemExit("GPT entry checksum mismatch")
    second = entries[ENTRY_SIZE:2 * ENTRY_SIZE]
    if uuid.UUID(bytes_le=second[:16]) != LINUX_FS_GUID or \
            second[56:66].decode("utf-16le") != "xv6fs":
        raise SystemExit("xv6fs partition contract mismatch")
    boot = data[BOOT_FIRST * SECTOR:(BOOT_FIRST + BOOT_SECTORS) * SECTOR]
    if boot[54:62] != b"FAT16   " or boot[510:512] != b"\x55\xaa":
        raise SystemExit("FAT16 boot partition missing")
    size = struct.unpack_from("<I", boot, (1 + 2 * 129) * SECTOR + 28)[0]
    payload_offset = (1 + 2 * 129 + 32) * SECTOR
    payload = boot[payload_offset:payload_offset + size]
    if expected_fit and hashlib.sha256(payload).digest() != hashlib.sha256(
            expected_fit.read_bytes()).digest():
        raise SystemExit("FAT payload differs from FIT input")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    make = sub.add_parser("build")
    make.add_argument("--fit", type=pathlib.Path, required=True)
    make.add_argument("--fs", type=pathlib.Path, required=True)
    make.add_argument("--output", type=pathlib.Path, required=True)
    check = sub.add_parser("inspect")
    check.add_argument("--image", type=pathlib.Path, required=True)
    check.add_argument("--fit", type=pathlib.Path)
    args = parser.parse_args()
    if args.command == "build":
        build(args.output, args.fit, args.fs)
        inspect(args.output, args.fit)
    else:
        inspect(args.image, args.fit)


if __name__ == "__main__":
    main()
