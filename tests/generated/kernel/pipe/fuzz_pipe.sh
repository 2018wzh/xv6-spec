#!/usr/bin/env sh
# kernel/pipe fixed-seed fuzz driver. Compiles the real kernel/pipe.c +
# kernel/file.c against the deterministic single-threaded harness (pipe_test.c)
# with host cc, and runs the fixed-seed bounded write/read/wraparound/peer-
# close workload. The harness mirrors the kernel/pipe invariants
# (pipe-capacity-bound, pipe-fifo-order, pipe-peer-close-termination) and
# writes only inside the current free space so no operation reaches the
# blocking path in the single-threaded model. Persists a deterministic
# reproduction artifact. Runs with cwd = project root; PATH explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/pipe_test" "$dir/pipe_test.c" \
   kernel/pipe.c kernel/file.c

if ! "$tmp/pipe_test" "$seed" "$cases"; then
  echo "kernel/pipe fuzz failed" >&2
  exit 1
fi

printf 'kernel_pipe_fuzz seed=%s cases=%s\n' "$seed" "$cases" > "$repro"
