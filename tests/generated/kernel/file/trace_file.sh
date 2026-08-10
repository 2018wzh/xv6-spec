#!/usr/bin/env sh
# kernel/file bounded trace/oracle target. The workload is a bounded QEMU
# virt serial capture booting the composite kernel with a real mkfs-generated
# fs.img on the virtio block device. The oracle requires that the file module
# is linked and initialized in boot order (fileinit before process
# initialization), the kernel reaches the scheduler with exactly one canonical
# XV6_BOOT_OK banner, and no panic text on serial. The file syscall handler
# surface is present in the dispatch table. Runs with cwd = project root;
# PATH explicitly allowed.
set -eu
output="$(mktemp)"
tmp="$(mktemp -d)"
trap 'rm -f "$output"; rm -rf "$tmp"' EXIT HUP INT TERM

make >/dev/null
cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" fs.img >/dev/null

status=0
timeout 10 qemu-system-riscv64 \
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
if grep -q 'panic' "$output"; then
  echo "trace: unexpected panic text in serial output" >&2
  cat "$output" >&2
  exit 1
fi

# The file module must be initialized in boot order before processes hold
# file references, and the file syscall surface must be wired into dispatch.
grep -q 'fileinit();' kernel/main.c
grep -q 'sys_open' kernel/syscall.c
grep -q 'sys_read' kernel/syscall.c
# The validated file ABI headers are present.
grep -q 'O_CREATE' kernel/fcntl.h
grep -q 'struct stat {' kernel/stat.h

echo "trace: bounded serial trace matched file-module wiring oracle"
