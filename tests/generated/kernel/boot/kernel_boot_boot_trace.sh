#!/usr/bin/env sh
# kernel_boot bounded trace/oracle target. Workload: a bounded QEMU virt
# serial capture of the booted Lab 2 kernel via vos_lab2_capture_serial.
# Oracle: the captured serial must contain exactly one XV6_BOOT_OK banner
# (canonical shared single-banner oracle). Reuses tests/public/lab2-boot.sh.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"
vos_lab2_require_single_banner "$output"
echo "trace: bounded boot serial matched single-banner oracle"
