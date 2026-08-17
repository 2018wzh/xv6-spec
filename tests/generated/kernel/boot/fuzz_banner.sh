#!/usr/bin/env sh
# kernel_boot fixed-seed fuzz driver. Compiles the deterministic, fixed-seed
# banner fuzz harness with the host `cc` and runs it with the given seed,
# case count, and a reproduction artifact path (written only on failure).
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -O2 -Wall -o "$tmp/banner_fuzz" \
  tests/generated/kernel/boot/banner_fuzz.c

"$tmp/banner_fuzz" "$seed" "$cases" "$repro"