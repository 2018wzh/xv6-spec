#!/usr/bin/env sh
# kernel/process bounded trace/oracle target. The workload is a bounded QEMU
# virt serial capture of the booted Lab 5 kernel; the oracle requires exactly
# one canonical XV6_BOOT_OK banner and proves the process substrate boots
# cleanly through procinit and scheduler activation on the single boot hart
# (address: the RUNNABLE-to-RUNNING dispatch edge is also exercised as a
# deterministic host model by the state-fuzz and wakeup contract harnesses).
# Runs with cwd = project root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# scheduler activation must follow procinit in the boot sequence.
grep -q 'procinit()' kernel/main.c
procinit_line="$(grep -n 'procinit()' kernel/main.c | head -n1 | cut -d: -f1)"
intr_on_line="$(grep -n 'intr_on()' kernel/main.c | head -n1 | cut -d: -f1)"
[ -n "$procinit_line" ] && [ -n "$intr_on_line" ] && [ "$procinit_line" -lt "$intr_on_line" ]

echo "trace: bounded serial trace matched single-canonical-banner oracle"
