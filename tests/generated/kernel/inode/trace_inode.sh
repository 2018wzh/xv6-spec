#!/usr/bin/env sh
# kernel/inode bounded trace/oracle target. The workload is a bounded QEMU
# virt serial capture booting the composite kernel with a real mkfs-generated
# fs.img on the virtio block device. The oracle requires that fsinit mounts
# the deterministic root image (filesystem-admission-order), the kernel reaches
# the scheduler with exactly one canonical XV6_BOOT_OK banner, and no panic text
# on serial. Runs with cwd = project root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
tmp="$(mktemp -d)"
trap 'rm -f "$output"; rm -rf "$tmp"' EXIT HUP INT TERM

# Build the composite kernel and the deterministic root image.
make >/dev/null
cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" fs.img >/dev/null

status=0
timeout 8 qemu-system-riscv64 \
  -machine virt \
  -bios none \
  -kernel kernel/kernel \
  -m 128M \
  -smp 1 \
  -nographic \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  </dev/null >"$output" 2>&1 || status=$?
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
  echo "trace: boot failed (status $status)" >&2
  cat "$output" >&2
  exit 1
fi

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# No panic text on serial (mount must succeed and reach the scheduler).
if grep -q 'panic' "$output"; then
  echo "trace: unexpected panic text in serial output" >&2
  cat "$output" >&2
  exit 1
fi

# The mount and path-resolution wiring is present in the owned source: the
# file-system layout, the inode cache, and component-wise traversal helpers.
grep -q 'struct superblock' kernel/fs.h
grep -q 'struct inode inode\[NINODE\]' kernel/fs.c
grep -q 'dirlookup' kernel/fs.c
grep -q 'skipelem' kernel/fs.c
grep -q 'fsinit' kernel/main.c

echo "trace: bounded serial trace matched inode mount oracle"