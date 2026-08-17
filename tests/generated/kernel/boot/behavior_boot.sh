#!/usr/bin/env sh
# kernel_boot bootstrap public behaviour: build and boot the Lab 2 kernel in
# QEMU and require the stored banner to be the single canonical XV6_BOOT_OK
# line. Reuses the shared public framework (tests/public/lab2-boot.sh).
# Runs with cwd = project root; PATH is explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"

# Exactly one XV6_BOOT_OK banner must be observed.
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]

# Every line that carries the banner must be exactly the canonical banner
# (no partial/garbage banner line can satisfy the single-banner oracle).
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]
echo "public: deterministic single canonical XV6_BOOT_OK banner observed"