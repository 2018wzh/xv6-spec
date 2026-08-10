#!/usr/bin/env sh
# kernel/bio fixed-seed LRU fuzz driver. Compiles the real kernel/bio.c
# against the deterministic single-threaded harness (lru_test.c) with host
# cc, then runs the fixed-seed acquire/release/pin workload. The harness
# mirrors the kernel/bio invariants (cache-identity-unique, active-buffer-
# not-evicted, cache-reference-nonnegative) and panics are intercepted so the
# expected error-path checks run without aborting. Runs with cwd = project
# root; PATH explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Compile the real kernel/bio.c together with the harness. -ffreestanding and
# -fno-builtin match the kernel build flags and keep host cc from pulling the
# stdio/string builtin declarations that conflict with defs.h.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/lru_test" "$dir/lru_test.c" kernel/bio.c

if ! "$tmp/lru_test" "$seed" "$cases" "$repro"; then
  echo "lru fuzz failed" >&2
  exit 1
fi