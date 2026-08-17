#!/usr/bin/env sh
# kernel/bio public boot test. The buffer cache is landed as source for this
# Lab 6 slice (its bread/bwrite/brelse wiring into main and defs.h belongs to
# the log/filesystem slices); this check verifies that the owned source is
# present, that the existing storage-layered kernel still builds, and that a
# connected virtio block device boots to the scheduler with exactly one
# canonical XV6_BOOT_OK banner and no panic, before the cache is composed in.
# A bio/buf regression (e.g. a broken header or missing owned file) fails the
# build or source-presence checks. Runs with cwd = project root; PATH allowed.
set -eu
output="$(mktemp)"
disk="$(mktemp)"
trap 'rm -f "$output" "$disk"' EXIT HUP INT TERM
truncate -s 1M "$disk"

# Owned source must exist (a missing implementation file is not "passed").
[ -f kernel/bio.c ] || { echo "public: missing kernel/bio.c" >&2; exit 1; }
[ -f kernel/buf.h ] || { echo "public: missing kernel/buf.h" >&2; exit 1; }

# The kernel must still build (bio source is not yet linked into main).
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
  echo "public: boot failed (status $status)" >&2
  cat "$output" >&2
  exit 1
fi

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# The buffered logical-block unit must be 1024 bytes and the cache fixed to
# NBUF entries (kernel/bio algorithm_intent and one-buffer-per-block).
grep -q '#define BSIZE 1024' kernel/buf.h
grep -q 'struct buf buf\[NBUF\]' kernel/bio.c

echo "public: bio source present, kernel boots with disk, single banner observed"