#!/usr/bin/env sh
# kernel_boot bounded trace/oracle target. The workload is a bounded QEMU
# serial capture; the oracle requires exactly one XV6_BOOT_OK banner.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"
vos_lab2_require_single_banner "$output"
echo "trace: bounded QEMU trace matched single-banner oracle"