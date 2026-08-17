#!/usr/bin/env sh
# kernel/syscall fixed-seed dispatch fuzz driver. Compiles the deterministic
# dispatch-bounds / argument-fetch fuzz harness with the host `cc` and runs it
# with the given seed, case count, and a reproduction artifact path written
# only on failure. Runs with cwd = project root; PATH explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

cc -O2 -Wall -o "$tmp/dispatch_fuzz" \
  tests/generated/kernel/syscall/dispatch_fuzz.c

"$tmp/dispatch_fuzz" "$seed" "$cases" "$repro"