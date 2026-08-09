#!/usr/bin/env sh
# kernel/memory public bounded boot check: build and boot the Lab 3 kernel
# in QEMU and require the single deterministic XV6_BOOT_OK banner, proving
# that kinit + kvminit + kvminithart all succeeded (a page-table activation
# or panic before banner publication would produce no banner or a garbled
# one). Reuses the shared public framework; runs with cwd = project root and
# PATH explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"

# The memory bootstrap runs in main before banner publication; an allocator
# or page-table failure panics silently (no banner), so exactly one canonical
# banner line must be observed.
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]

banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# The memory bootstrap must be wired into main before banner publication.
grep -q 'kinit()' kernel/main.c
grep -q 'kvminit()' kernel/main.c
grep -q 'kvminithart()' kernel/main.c
grep -n 'kinit()' kernel/main.c > /dev/null
first_mem="$(grep -n 'kinit()\|export' kernel/main.c | head -n1 | cut -d: -f1)"
banner_line="$(grep -n 'publish_boot_banner()' kernel/main.c | head -n1 | cut -d: -f1)"
[ -n "$first_mem" ] && [ -n "$banner_line" ] && [ "$first_mem" -lt "$banner_line" ]

echo "public: single banner observed; memory bootstrap precedes banner"
