#!/usr/bin/env sh
# kernel/file public check (kernel_file_syscall_public).
#
# Extends the file-module public surface: verifies the bounded Lab 6 user
# workload (user/fstest.c) compiles against the owned user ABI headers
# (kernel/stat.h, kernel/fcntl.h, kernel/user.ld) through the Makefile
# user-fstest target, confirms the validated file syscall handlers and their
# numbers are wired into the dispatch table, and verifies the composite
# kernel still builds and boots a deterministic fs.img with exactly one
# XV6_BOOT_OK banner and no panic. Runs with cwd = project root; PATH allowed.
set -eu
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# The bounded user file-ABI workload must compile as a freestanding RISC-V
# binary linked with the owned kernel/user.ld (validates the user ABI surface).
make user-fstest >/dev/null
[ -f user/_fstest ] || { echo "public: user/_fstest missing" >&2; exit 1; }

# The registered user-syscall boundary must expose the file ABI surface.
grep -q 'SYS_open' kernel/syscall.h

grep -q 'sys_open' kernel/syscall.c
grep -q 'sys_read' kernel/syscall.c
grep -q 'sys_write' kernel/syscall.c
grep -q 'sys_close' kernel/syscall.c
grep -q 'sys_fstat' kernel/syscall.c
grep -q 'sys_dup' kernel/syscall.c

grep -q 'sys_mkdir' kernel/syscall.c
grep -q 'sys_unlink' kernel/syscall.c
grep -q 'sys_link' kernel/syscall.c
grep -q 'sys_chdir' kernel/syscall.c

# The validated file ABI headers are present and stable.
grep -q 'O_CREATE' kernel/fcntl.h
grep -q 'O_TRUNC' kernel/fcntl.h
grep -q 'struct stat {' kernel/stat.h

# The composite kernel still builds and boots a deterministic image.
make kernel/kernel >/dev/null
cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" "$tmp/fs.img" >/dev/null

status=0
timeout 10 qemu-system-riscv64 \
  -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic \
  -drive file="$tmp/fs.img",if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  </dev/null >"$tmp/out" 2>&1 || status=$?
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
  echo "public: boot failed (status $status)" >&2
  cat "$tmp/out" >&2
  exit 1
fi
count="$(grep -o 'XV6_BOOT_OK' "$tmp/out" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
if grep -q 'panic' "$tmp/out"; then
  echo "public: panic while booting fs.img" >&2
  cat "$tmp/out" >&2
  exit 1
fi

echo "kernel_file_syscall_public: file ABI surface and bounded workload compile; kernel boots"
