#!/usr/bin/env sh
# kernel/process fixed-seed state-fuzz driver. Compiles the deterministic
# lifecycle-state fuzz harness with `cc` and runs it with the given seed,
# case count, and a reproduction artifact path written only on failure.
# The harness validates the RUNNABLE/RUNNING/SLEEPING edge oracle the kernel
# scheduler and sleep/wakeup enforce. Runs with cwd = project root; PATH is
# explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

cc -O2 -Wall -o "$tmp/fuzz_state" \
  tests/generated/kernel/process/fuzz_state.c

"$tmp/fuzz_state" "$seed" "$cases" "$repro"
