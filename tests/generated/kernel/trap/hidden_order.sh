#!/usr/bin/env sh
# kernel_trap hidden: full interrupt-enable-order chain. Verifies that in
# main() every trap-stage initialization (trapinit, plicinit, plicinithart,
# uartinit, consoleinit) is wired before intr_on() enables supervisor
# interrupts, and that the boot banner publication precedes the trap stage.
# This is stricter than the public test (it checks all five init calls ahead
# of intr_on and their relative order).
set -eu

# gather the line numbers of each required stage in main().
kinit_l="$(grep -n 'kinit()' kernel/main.c | head -n1 | cut -d: -f1)"
banner_l="$(grep -n 'publish_boot_banner()' kernel/main.c | head -n1 | cut -d: -f1)"
trapinit_l="$(grep -n 'trapinit()' kernel/main.c | head -n1 | cut -d: -f1)"
plicinit_l="$(grep -n 'plicinit()' kernel/main.c | head -n1 | cut -d: -f1)"
plic_inithart_l="$(grep -n 'plicinithart()' kernel/main.c | head -n1 | cut -d: -f1)"
uartinit_l="$(grep -n 'uartinit()' kernel/main.c | head -n1 | cut -d: -f1)"
consoleinit_l="$(grep -n 'consoleinit()' kernel/main.c | head -n1 | cut -d: -f1)"
intr_on_l="$(grep -n 'intr_on()' kernel/main.c | head -n1 | cut -d: -f1)"

for v in "$kinit_l" "$banner_l" "$trapinit_l" "$plicinit_l" "$plic_inithart_l" "$uartinit_l" "$consoleinit_l" "$intr_on_l"; do
  [ -n "$v" ]
done

# memory -> banner -> trap vector -> PLIC -> UART -> console -> intr_on.
[ "$kinit_l" -lt "$banner_l" ]
[ "$banner_l" -lt "$trapinit_l" ]
[ "$trapinit_l" -lt "$plicinit_l" ]
[ "$plicinit_l" -lt "$plic_inithart_l" ]
[ "$plic_inithart_l" -lt "$uartinit_l" ]
[ "$uartinit_l" -lt "$consoleinit_l" ]
[ "$consoleinit_l" -lt "$intr_on_l" ]

echo "hidden: full trap interrupt-enable-order chain verified"