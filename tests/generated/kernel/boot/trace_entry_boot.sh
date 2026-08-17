#!/usr/bin/env sh
# kernel_boot bounded trace/oracle target. The workload is a bounded QEMU
# virt serial capture of the booted Lab 2 kernel via vos_lab2_capture_serial;
# the oracle requires exactly one canonical XV6_BOOT_OK banner line and no
# partial/garbage banner line in the captured serial. Runs with cwd = project
# root; PATH is explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]
echo "trace: bounded serial trace matched single-canonical-banner oracle"