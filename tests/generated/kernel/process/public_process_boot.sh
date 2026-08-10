#!/usr/bin/env sh
# kernel/process public check. The compiled Lab 5 kernel initializes the
# process table (procinit assigns each slot an index-keyed kernel stack and a
# lock, leaving every slot UNUSED) before the scheduler is activated; a clean
# QEMU virt boot must publish exactly one canonical XV6_BOOT_OK banner. Runs
# with cwd = project root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# Process table is initialized (main calls procinit) before scheduler runs.
grep -q 'procinit()' kernel/main.c
grep -q 'scheduler()' kernel/main.c
procinit_line="$(grep -n 'procinit()' kernel/main.c | head -n1 | cut -d: -f1)"
scheduler_line="$(grep -n 'scheduler()' kernel/main.c | head -n1 | cut -d: -f1)"
[ -n "$procinit_line" ] && [ -n "$scheduler_line" ] && [ "$procinit_line" -lt "$scheduler_line" ]

echo "ok: process table initialized before scheduler activation"
