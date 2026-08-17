#!/usr/bin/env sh
# kernel_trap bounded trace/oracle target. The workload is a bounded QEMU virt
# serial capture of the booted Lab 4 kernel (vector installed, PLIC/UART
# configured, supervisor interrupts enabled); the oracle requires exactly one
# canonical XV6_BOOT_OK banner line and that the trap vector is installed in
# stvec before supervisor interrupts are enabled, so a clean boot through
# vector installation and bounded interrupt dispatch is proven without any
# input-driven interrupt actually firing. Runs with cwd = project root; PATH
# explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# The vector must be installed before interrupts are enabled.
trapinit_line="$(grep -n 'trapinit()' kernel/main.c | head -n1 | cut -d: -f1)"
intr_on_line="$(grep -n 'intr_on()' kernel/main.c | head -n1 | cut -d: -f1)"
[ -n "$trapinit_line" ] && [ -n "$intr_on_line" ] && [ "$trapinit_line" -lt "$intr_on_line" ]

echo "trace: clean boot through vector installation and single-banner oracle"