#!/usr/bin/env sh
# kernel/bio contract check. Compiles the real kernel/bio.c together with the
# deterministic single-threaded harness (lru_test.c) and runs the cached-contract
# mode (no arguments) that exercises bread/bwrite/brelse/bpin/bunpin against the
# kernel/bio invariants:
#   - one-buffer-per-block / cache-identity-unique: one identity per (dev,block),
#     and reacquiring a cached block returns the same buffer.
#   - active-buffer-not-evicted; cache-reference-nonnegative.
#   - bio-write-contract: bwrite preserves identity and does not release the lock.
#   - bio-pin-lifetime-contract: bpin survives brelse and prevents eviction.
#   - errors: double unpin and invalid lock ownership panic.
# Runs with cwd = project root; PATH explicitly allowed.
set -eu
dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Compile the real kernel/bio.c with the harness. -ffreestanding/-fno-builtin
# match the kernel build flags and avoid host-builtin declaration conflicts.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/lru_test" "$dir/lru_test.c" kernel/bio.c

if ! "$tmp/lru_test"; then
  echo "bio contract failed" >&2
  exit 1
fi