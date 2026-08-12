#!/usr/bin/env sh
# kernel/pipe public check. Verifies that the owned pipe source is present
# (kernel/pipe.c) and linked into the build projection (Makefile), that the
# pipe operation surface is declared (pipealloc/piperead/pipewrite/pipeclose in
# kernel/defs.h), that the composite kernel still builds and boots with exactly
# one canonical XV6_BOOT_OK banner and no panic, and that the pipe host harness
# compiles against the real owned sources. Runs with cwd = project root; PATH
# explicitly allowed.
set -eu
output="$(mktemp)"
tmp="$(mktemp -d)"
trap 'rm -f "$output"; rm -rf "$tmp"' EXIT HUP INT TERM

[ -f kernel/pipe.c ] || { echo "public: missing kernel/pipe.c" >&2; exit 1; }

# The pipe module is wired into the kernel build and declared in defs.h.
grep -q 'pipe.o' Makefile
for fn in pipealloc pipewrite piperead pipeclose; do
  grep -q "$fn" kernel/defs.h || {
    echo "public: missing $fn declaration in defs.h" >&2; exit 1; }
done

# The composite kernel must still build and boot with the pipe module linked.
make

# Use the mkfs-generated deterministic image if present; reuse an existing
# fs.img, otherwise build a fresh one with the host mkfs.
if [ ! -f fs.img ]; then
  cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
  "$tmp/mkfs" fs.img >/dev/null 2>&1 || echo "warning: mkfs failed; booting without disk image"
fi

status=0
if [ -f fs.img ]; then
  timeout 10 qemu-system-riscv64 \
    -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
    </dev/null >"$output" 2>&1 || status=$?
else
  timeout 10 qemu-system-riscv64 \
    -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic \
    </dev/null >"$output" 2>&1 || status=$?
fi
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
  echo "public: boot failed (status $status)" >&2
  cat "$output" >&2
  exit 1
fi
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
if grep -q 'panic' "$output"; then
  echo "public: panic while booting" >&2
  cat "$output" >&2
  exit 1
fi

# The host harness must compile against the real owned sources.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/pipe_test" tests/generated/kernel/pipe/pipe_test.c \
   kernel/pipe.c kernel/file.c

echo "public: pipe source present, pipe surface linked, kernel boots, harness compiles"
