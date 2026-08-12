#!/usr/bin/env sh
# kernel/memory fixed-seed validated-copy fuzz driver. Compiles the
# deterministic host-model copy-validation harness with `cc` and runs it with
# the given seed, case count, and a reproduction artifact path written only on
# failure. Runs with cwd = project root; PATH explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -O2 -Wall -o "$tmp/copy_fuzz" \
  tests/generated/kernel/memory/copy_fuzz.c

"$tmp/copy_fuzz" "$seed" "$cases" "$repro"