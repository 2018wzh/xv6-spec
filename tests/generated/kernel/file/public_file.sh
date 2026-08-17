#!/usr/bin/env sh
# kernel/file public check. Verifies that the owned file sources are present
# (kernel/file.c, kernel/file.h, kernel/sysfile.c, kernel/stat.h,
# kernel/fcntl.h, kernel/user.ld, user/), that the file ABI surface is
# declared, that the composite kernel still builds and boots a deterministic
# fs.img with exactly one canonical XV6_BOOT_OK banner and no panic, and that
# the host harness compiles against the real owned sources. Runs with cwd =
# project root; PATH allowed.
set -eu
output="$(mktemp)"
tmp="$(mktemp -d)"
trap 'rm -f "$output"; rm -rf "$tmp"' EXIT HUP INT TERM

[ -f kernel/file.c ] || { echo "public: missing kernel/file.c" >&2; exit 1; }
[ -f kernel/file.h ] || { echo "public: missing kernel/file.h" >&2; exit 1; }
[ -f kernel/sysfile.c ] || { echo "public: missing kernel/sysfile.c" >&2; exit 1; }
[ -f kernel/stat.h ] || { echo "public: missing kernel/stat.h" >&2; exit 1; }
[ -f kernel/fcntl.h ] || { echo "public: missing kernel/fcntl.h" >&2; exit 1; }
[ -f kernel/user.ld ] || { echo "public: missing kernel/user.ld" >&2; exit 1; }

# The file syscall surface is wired into the dispatch table and build.
grep -q 'sys_read' kernel/syscall.c
grep -q 'sys_open' kernel/syscall.c
grep -q 'sysfile.o' Makefile

# The composite kernel must still build and boot with the file module linked.
make

cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" fs.img >/dev/null

status=0
timeout 10 qemu-system-riscv64 \
  -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  </dev/null >"$output" 2>&1 || status=$?
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
  echo "public: boot failed (status $status)" >&2
  cat "$output" >&2
  exit 1
fi
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
if grep -q 'panic' "$output"; then
  echo "public: panic while booting fs.img" >&2
  cat "$output" >&2
  exit 1
fi

# The host harness must compile against the real owned sources.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/file_test" tests/generated/kernel/file/file_test.c \
   kernel/fs.c kernel/bio.c kernel/log.c kernel/file.c kernel/sysfile.c

echo "public: file sources present, surface declared, kernel boots, harness compiles"
