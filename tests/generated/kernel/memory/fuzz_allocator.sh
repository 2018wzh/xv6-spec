#!/usr/bin/env sh
# kernel/memory fixed-seed allocator-contract fuzz driver. Compiles the
# deterministic host-model fuzz harness with `cc` and runs it with the given
# seed, case count, and a reproduction artifact path written only on failure.
# Runs with cwd = project root; PATH is explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -O2 -Wall -o "$tmp/allocator_fuzz" \
  tests/generated/kernel/memory/allocator_fuzz.c

"$tmp/allocator_fuzz" "$seed" "$cases" "$repro"
