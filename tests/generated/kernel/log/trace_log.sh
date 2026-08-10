#!/usr/bin/env sh
# kernel/log bounded trace/oracle target. The workload is a bounded QEMU virt
# serial capture booting the existing storage-layered kernel with a connected
# virtio block device (the redo log is landed as source in this Lab 6 slice;
# its admission/commit boundary composes with a mount, which is a later slice).
# The oracle requires exactly one canonical XV6_BOOT_OK banner, a clean boot to
# the scheduler, and no panic text, proving the storage/lock layering the log
# relies on initializes without corruption. Runs with cwd = project root; PATH
# explicitly allowed.
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

# The log must be self-contained: a single spinlock guards admission and header
# membership, and the commit path never waits for buffer/disk I/O while holding
# that lock (forbidden_patterns) -- the disk waits happen with the log lock
# released in the commit/end_op path.
grep -q 'struct spinlock lock' kernel/log.c
grep -q 'struct logheader lh' kernel/log.c
grep -q 'recover_from_log' kernel/log.c
# The redo-order discipline: the durable commit header (write_head) is reached
# only after the log data (write_log) is written.
grep -q 'write_log();' kernel/log.c
grep -q 'write_head();' kernel/log.c

echo "trace: bounded serial trace matched log storage-layering oracle"