#!/usr/bin/env sh
# kernel_boot immutable-banner fuzz driver. Links the host harness against
# the real kernel/boot.c (boot_banner) using -Ikernel, then runs it with a
# fixed seed, case count, and a reproduction artifact path (written only on
# failure). Runs with cwd = project root; PATH is explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -O2 -Wall -fno-builtin -Ikernel \
  -c kernel/boot.c -o "$tmp/boot.o"

cc -O2 -Wall -fno-builtin -Ikernel \
  -c tests/generated/kernel/boot/banner_immutable_fuzz.c -o "$tmp/fuzz.o"

cc -o "$tmp/banner_immutable" \
  "$tmp/boot.o" "$tmp/fuzz.o"

"$tmp/banner_immutable" "$seed" "$cases" "$repro"