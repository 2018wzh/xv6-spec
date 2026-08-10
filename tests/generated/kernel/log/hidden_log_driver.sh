#!/usr/bin/env sh
# kernel/log hidden-test driver. Compiles the projected hidden-test C source
# (a {hidden_test} path) together with the real kernel/log.c and kernel/bio.c
# against the host cc with the single-threaded stubs the harness supplies, then
# runs the resulting binary. Runs with cwd = project root; PATH explicitly
# allowed.
set -eu
hidden_src="${1:?hidden_test source path required}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -O2 -Wall -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/hidden_log" "$hidden_src" kernel/log.c kernel/bio.c

"$tmp/hidden_log"