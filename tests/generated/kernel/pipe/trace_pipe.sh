#!/usr/bin/env sh
# kernel/pipe bounded trace/oracle target. The workload is a bounded QEMU virt
# serial capture booting the composite kernel with the pipe module linked. The
# oracle requires exactly one canonical XV6_BOOT_OK banner, no panic text on
# serial, and that the pipe module's operation surface is wired into the kernel
# (pipe.o in Makefile, pipe function declarations in defs.h, sys_pipe in
# sysfile.c / syscall.c). Runs with cwd = project root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
if [ ! -f fs.img ]; then
  cc -O2 -Wall -Wextra -Werror -I kernel -o "${output%.out}.mkfs" mkfs/mkfs.c
  "${output%.out}.mkfs" fs.img >/dev/null 2>&1
fi
if [ ! -f kernel/kernel ] || [ -n "$(find kernel -name '*.c' -newer kernel/kernel | head -1)" ]; then
  make >/dev/null
fi

trap 'rm -f "$output" "${output%.out}.mkfs"' EXIT HUP INT TERM

status=0
timeout 10 qemu-system-riscv64 \
  -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic \
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

# The pipe surface must be wired into the kernel build and dispatch.
grep -q 'pipe.o' Makefile
grep -q 'pipealloc' kernel/pipe.c
grep -q 'pipewrite' kernel/pipe.c
grep -q 'piperead' kernel/pipe.c
grep -q 'pipeclose' kernel/pipe.c
# sys_pipe must be dispatched from sysfile.c / syscall.c.
grep -q 'sys_pipe' kernel/sysfile.c
grep -q 'SYS_pipe' kernel/syscall.c

# The file layer must forward FD_PIPE file operations to the pipe module.
grep -q 'FD_PIPE' kernel/file.c

echo "trace: bounded serial trace matched pipe-module wiring oracle"
