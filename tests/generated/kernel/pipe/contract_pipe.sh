#!/usr/bin/env sh
# kernel/pipe contract check. Compiles the real kernel/pipe.c + kernel/file.c
# together with the deterministic single-threaded harness (pipe_test.c) using
# Host cc with -Ikernel, and runs the contract mode that exercises the Lab 7
# pipe invariants:
#   - pipe-fifo-order: the bounded ring exposes every accepted byte exactly
#     once and in acceptance order across wraparound.
#   - pipe-capacity-bound: a single write cannot publish more bytes than the
#     fixed ring capacity.
#   - pipe-endpoint-reference-consistency: pipealloc publishes exactly two
#     file refs (one readable, one writable) sharing one pipe; each final
#     close updates endpoint liveness and the backing pipe is freed only
#     after both sides are closed.
#   - pipe-peer-close-termination: closing the final writer makes an empty
#     reader observe EOF; closing the final reader makes writers fail stably
#     (broken pipe).
# Runs with cwd = project root; PATH explicitly allowed.
set -eu
dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Compile the real kernel/pipe.c + kernel/file.c against the harness.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/pipe_test" "$dir/pipe_test.c" \
   kernel/pipe.c kernel/file.c

if ! "$tmp/pipe_test"; then
  echo "kernel/pipe contract failed" >&2
  exit 1
fi
