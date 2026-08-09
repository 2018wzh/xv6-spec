#!/usr/bin/env sh
# kernel_boot public bounded boot: build and boot QEMU, require the Lab 2
# banner to appear exactly once. Matches the public framework, available
# as a standalone bounded target. Runs with cwd = project root, PATH allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"
vos_lab2_require_single_banner "$output"
echo "public: single XV6_BOOT_OK banner observed"