#!/usr/bin/env sh
# kernel_boot contract check: the ns16550a is byte-addressed MMIO with
# THR at byte offset 0, LSR at byte offset 5, and transmitter-ready bit 5.
# Register offsets are never scaled by a C word size. Also requires the
# boot_banner entry point returning the XV6_BOOT_OK banner.
set -eu

# Byte-offset ABI constants.
grep -q 'UART_THR 0' kernel/boot.c
grep -q 'UART_LSR 5' kernel/boot.c
grep -q 'LSR_TX_READY (1 << 5)' kernel/boot.c

# Byte-addressed access: never a word-scaled pointer dereference.
grep -q 'volatile uchar \*uart' kernel/boot.c
grep -q 'uart\[UART_THR\]' kernel/boot.c
grep -q 'uart\[UART_LSR\]' kernel/boot.c

# Banner entry point returns an immutable banner containing XV6_BOOT_OK.
grep -q 'boot_banner(void)' kernel/boot.c
grep -q 'XV6_BOOT_OK' kernel/boot.c

echo "contract: UART byte-register ABI and boot_banner entry point present"