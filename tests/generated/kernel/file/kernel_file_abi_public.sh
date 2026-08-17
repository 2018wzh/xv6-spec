#!/usr/bin/env sh
# kernel/file public check (kernel_file_abi_public).
#
# Verifies that the owned file sources are present and that the composite
# kernel still builds and boots a deterministic mkfs-generated fs.img on the
# virtio block device with exactly one canonical XV6_BOOT_OK banner and no
# panic. Reuses the shared public kernel/file checks. Runs with cwd = project
# root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
tmp="$(mktemp -d)"
trap 'rm -f "$output"; rm -rf "$tmp"' EXIT HUP INT TERM

# The kernel/file owned surface must be present.
[ -f kernel/file.c ]  || { echo "kernel_file_abi_public: missing kernel/file.c" >&2; exit 1; }
[ -f kernel/file.h ]  || { echo "kernel_file_abi_public: missing kernel/file.h" >&2; exit 1; }
[ -f kernel/sysfile.c ] || { echo "kernel_file_abi_public: missing kernel/sysfile.c" >&2; exit 1; }
[ -f kernel/fcntl.h ] || { echo "kernel_file_abi_public: missing kernel/fcntl.h" >&2; exit 1; }
[ -f kernel/stat.h ]  || { echo "kernel_file_abi_public: missing kernel/stat.h" >&2; exit 1; }

# The file syscall surface is wired into the dispatch table and build.
grep -q 'sys_read' kernel/syscall.c
grep -q 'sys_open' kernel/syscall.c
grep -q 'sysfile.o' Makefile

# The composite kernel must still build and boot with the file module linked.
make >/dev/null

cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" fs.img >/dev/null

status=0
timeout 10 qemu-system-riscv64 \
  -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  </dev/null >"$output" 2>&1 || status=$?
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
  echo "kernel_file_abi_public: boot failed (status $status)" >&2
  cat "$output" >&2
  exit 1
fi
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
if grep -q 'panic' "$output"; then
  echo "kernel_file_abi_public: panic while booting fs.img" >&2
  cat "$output" >&2
  exit 1
fi

echo "kernel_file_abi_public: file module builds, boots a deterministic image, no panic"
