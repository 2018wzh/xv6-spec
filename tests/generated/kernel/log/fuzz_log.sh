#!/usr/bin/env sh
# kernel/log fixed-seed redo-log fuzz driver. Compiles the real kernel/log.c
# together with the real kernel/bio.c against the deterministic single-threaded
# harness (log_test.c) with host cc, then runs the fixed-seed admission/
# log_write/end_op workload. The harness mirrors the kernel/log invariants
# (log-admission-contract, logged-block-unique, recovery-idempotent) and panics
# are intercepted so the expected validation-error checks run without aborting.
# Runs with cwd = project root; PATH explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

# Compile the real kernel/log.c and kernel/bio.c together with the harness.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/log_test" "$dir/log_test.c" kernel/log.c kernel/bio.c

if ! "$tmp/log_test" "$seed" "$cases" "$repro"; then
  echo "log fuzz failed" >&2
  exit 1
fi