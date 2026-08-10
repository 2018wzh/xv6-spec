#!/usr/bin/env sh
# Shared Lab 2 public framework. Generated tests run with cwd set to the
# project root and may source this file instead of rebuilding QEMU helpers.

vos_lab2_build() {
  make
}

vos_lab2_capture_serial() {
  output="${1:?serial output path required}"
  vos_lab2_build
  status=0
  timeout "${VOS_LAB2_BOOT_TIMEOUT:-10}" qemu-system-riscv64 \
    -machine virt \
    -bios none \
    -kernel kernel/kernel \
    -m 128M \
    -smp 1 \
    -nographic \
    </dev/null >"$output" 2>&1 || status=$?
  if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    return "$status"
  fi
}

vos_lab2_require_single_banner() {
  output="${1:?serial output path required}"
  count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
  [ "$count" = "1" ]
}
