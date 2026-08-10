#!/usr/bin/env sh
# kernel/log contract check. Compiles the real kernel/log.c together with the
# real kernel/bio.c plus the deterministic single-threaded harness (log_test.c)
# and runs the contract mode (no arguments) that exercises initlog/begin_op/
# log_write/end_op and commutative recovery against the kernel/log invariants:
#   - log-admission-contract: outstanding ops + MAXOPBLOCKS never exceed LOGSIZE.
#   - logged-block-unique: log_write of a block consumes one slot per distinct
#     block; the header is cleared after the last end_op commits.
#   - redo-ordering / committed-header-boundary: log data reaches storage before
#     the nonempty commit header and home blocks change only after it.
#   - recovery-idempotent: a committed or interrupted-log image recovers twice
#     to identical metadata; repeating recovery is a no-op.
#   - errors: corrupt headers, impossible block numbers, and over-capacity
#     transactions panic before partial replay.
# Runs with cwd = project root; PATH explicitly allowed.
set -eu
dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Compile the real kernel/log.c and kernel/bio.c with the harness.
# -ffreestanding/-fno-builtin match the kernel build flags and avoid
# host-builtin declaration conflicts with defs.h.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/log_test" "$dir/log_test.c" kernel/log.c kernel/bio.c

if ! "$tmp/log_test"; then
  echo "log contract failed" >&2
  exit 1
fi