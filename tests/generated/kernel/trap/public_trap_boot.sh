#!/usr/bin/env sh
# kernel_trap public bounded boot check: build and boot the Lab 4 kernel in
# QEMU and require exactly one canonical XV6_BOOT_OK banner line, proving the
# boot-to-console handoff preserved the Lab 2 banner. Also verifies at the
# source level that PLIC and UART initialization precede setting the
# supervisor interrupt-enable bits, so the banner is only published once the
# boot output path is intact. Runs with cwd = project root; PATH allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"

# The trap-stage boot must preserve a single canonical banner.
count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# interrupt-enable-order: PLIC and UART init precede supervisor interrupt
# enable in main, and trapinit (stvec) is installed before SIE is set.
for tok in trapinit plicinit plicinithart uartinit consoleinit; do
  grep -q "$tok()" kernel/main.c
done
trapinit_line="$(grep -n 'trapinit()' kernel/main.c | head -n1 | cut -d: -f1)"
intr_on_line="$(grep -n 'intr_on()' kernel/main.c | head -n1 | cut -d: -f1)"
[ -n "$trapinit_line" ] && [ -n "$intr_on_line" ] && [ "$trapinit_line" -lt "$intr_on_line" ]
for tok in plicinit plicinithart uartinit; do
  line="$(grep -n "$tok()" kernel/main.c | head -n1 | cut -d: -f1)"
  [ -n "$line" ] && [ "$line" -lt "$intr_on_line" ]
done

echo "public: single canonical banner survives the trap-stage handoff"