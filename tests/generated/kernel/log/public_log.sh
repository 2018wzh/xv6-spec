#!/usr/bin/env sh
# kernel/log public check. The redo log is landed as source for this Lab 6
# slice (its begin_op/log_write/end_op wiring into a file-system mount belongs
# to the later filesystem slice, whose fsinit calls initlog/recover); this
# check verifies that the owned source is present, that the existing
# storage-layered kernel still builds, and that a connected virtio block device
# boots to the scheduler with exactly one canonical XV6_BOOT_OK banner and no
# panic. A log/defs regression (e.g. a broken header or missing owned declaration)
# fails the build or source-presence checks. Runs with cwd = project root;
# PATH allowed.
set -eu
output="$(mktemp)"
disk="$(mktemp)"
trap 'rm -f "$output" "$disk"' EXIT HUP INT TERM
truncate -s 1M "$disk"

# Owned source must exist (a missing implementation file is not "passed").
[ -f kernel/log.c ] || { echo "public: missing kernel/log.c" >&2; exit 1; }

# The log interface is exposed through the shared declaration header so the
# later file-system slice can call begin_op/log_write/end_op/initlog.
grep -q 'initlog(int);' kernel/defs.h
grep -q 'begin_op(void);' kernel/defs.h
grep -q 'log_write(struct buf \*);' kernel/defs.h
grep -q 'end_op(void);' kernel/defs.h

# The kernel must still build (log source is landed but composed with a mount
# in a later slice).
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

# The host harness must compile against the real owned sources.
compile_tmp="$(mktemp -d)"
trap 'rm -rf "$compile_tmp"' EXIT HUP INT TERM
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$compile_tmp/log_test" tests/generated/kernel/log/log_test.c \
   kernel/log.c kernel/bio.c

echo "public: log source present, interface declared, kernel boots, harness compiles"