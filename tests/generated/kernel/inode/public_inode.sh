#!/usr/bin/env sh
# kernel/inode public check. Verifies that the owned inode source is present
# (kernel/fs.c, kernel/fs.h, mkfs), that the inode interface is declared, and
# that the composite storage-layered kernel still builds and boots. It boots
# twice over QEMU serial capture:
#   1. with a real mkfs-generated fs.img on the virtio block device, expecting
#      fsinit to mount the deterministic root image without panic and reach the
#      scheduler with exactly one canonical XV6_BOOT_OK banner;
#   2. with a bare (unformatted) lab disk, expecting the kernel to reach the
#      scheduler with a single banner and no panic (filesystem-admission-order
#      keeps an unformatted root device unmounted rather than panicking).
# The harness also compiles against the real owned sources. Runs with cwd =
# project root; PATH allowed.
set -eu
output="$(mktemp)"
disk="$(mktemp)"
tmp="$(mktemp -d)"
trap 'rm -rf "$output" "$disk" "$tmp"' EXIT HUP INT TERM

# Owned source must exist (a missing implementation file is not "passed").
[ -f kernel/fs.c ] || { echo "public: missing kernel/fs.c" >&2; exit 1; }
[ -f kernel/fs.h ] || { echo "public: missing kernel/fs.h" >&2; exit 1; }
[ -f mkfs/mkfs.c ] || { echo "public: missing mkfs/mkfs.c" >&2; exit 1; }

# The inode interface is exposed through defs.h and the owned fs.h.
grep -q 'fsinit(int);' kernel/defs.h
grep -q 'fsinit' kernel/fs.h

# Build the composite kernel and a deterministic image.
make >/dev/null
cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" fs.img >/dev/null

boot_capture() {
  local img="$1" out="$2"
  local status=0
  timeout 8 qemu-system-riscv64 \
    -machine virt \
    -bios none \
    -kernel kernel/kernel \
    -m 128M \
    -smp 1 \
    -nographic \
    -drive file="$img",if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
    </dev/null >"$out" 2>&1 || status=$?
  [ "$status" -eq 0 ] || [ "$status" -eq 124 ] || {
    echo "public: boot failed (status $status)" >&2
    cat "$out" >&2
    return 1
  }
}

# 1) Boot with the deterministic mounted image.
if ! boot_capture fs.img "$output"; then exit 1; fi
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
if grep -q 'panic' "$output"; then
  echo "public: panic while mounting fs.img" >&2
  cat "$output" >&2
  exit 1
fi

# 2) Boot with a bare (unformatted) lab disk.
truncate -s 1M "$disk"
if ! boot_capture "$disk" "$output"; then exit 1; fi
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
if grep -q 'panic' "$output"; then
  echo "public: panic on unformatted root disk" >&2
  cat "$output" >&2
  exit 1
fi

# The public harness must compile against the real owned sources.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/fs_test" tests/generated/kernel/inode/fs_test.c \
   kernel/fs.c kernel/bio.c kernel/log.c

echo "public: inode source present, interface declared, image mounts + bare disk boots, harness compiles"