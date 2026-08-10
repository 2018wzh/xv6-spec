#!/usr/bin/env sh
# kernel/syscall public target. Builds the kernel and boots it under QEMU virt,
# verifying the Lab 5 syscall composition keeps the deterministic single-banner
# boot contract, and that the syscall module's owned sources are present and
# linked (syscall.c / sysproc.c dispatch table and trampoline.S uservec/userret).
# Runs with cwd = project root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]

# The syscall module's owned files and symbols must be present and linked.
for f in kernel/syscall.c kernel/syscall.h kernel/sysproc.c kernel/trampoline.S; do
  [ -f "$f" ] || { echo "public: missing $f" >&2; exit 1; }
done
grep -q 'syscall(void)' kernel/syscall.c
grep -q 'uservec:' kernel/trampoline.S
grep -q 'userret:' kernel/trampoline.S

# The kernel image must actually contain the trampoline entry points.
TOOLCHAIN="$(sh tests/generated/toolchain/select_toolchain.sh)"
"${TOOLCHAIN}objdump" -t kernel/kernel | grep -q 'uservec'
"${TOOLCHAIN}objdump" -t kernel/kernel | grep -q 'userret'

echo "public: single-banner boot with syscall surface linked"