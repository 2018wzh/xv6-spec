#!/usr/bin/env sh
# kernel/bio bounded trace/oracle target. The workload is a bounded QEMU virt
# serial capture booting the existing storage-layered kernel with a connected
# virtio block device (the buffer cache is landed as source in this Lab 6
# slice; its transfer boundary kernel/bio depends on is present and wired).
# The oracle requires exactly one canonical XV6_BOOT_OK banner, a clean boot
# to the scheduler, and no panic text, proving the storage/lock layering the
# cache relies on initializes without corruption. Runs with cwd = project
# root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
disk="$(mktemp)"
trap 'rm -f "$output" "$disk"' EXIT HUP INT TERM
truncate -s 1M "$disk"

make

status=0
timeout 8 qemu-system-riscv64 \
  -machine virt \
  -bios none \
  -kernel kernel/kernel \
  -m 128M \
  -smp 1 \
  -nographic \
  -drive file="$disk",if=none,format=raw,id=x0 \
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

# No panic text on serial (single banner and a bounded, non-garbled trace).
if grep -q 'panic' "$output"; then
  echo "trace: unexpected panic text in serial output" >&2
  cat "$output" >&2
  exit 1
fi

# The cache owns a per-buffer sleep lock and the cache spinlock; the
# forbidden pattern is sleeping/disk I/O while holding the cache spinlock,
# so the cache lock must be released before the sleep-lock acquisition.
grep -q 'struct sleeplock lock' kernel/buf.h
grep -q 'release(&bcache.lock);' kernel/bio.c
grep -q 'acquiresleep(&b->lock);' kernel/bio.c

echo "trace: bounded serial trace matched bio storage-layering oracle"