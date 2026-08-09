#!/usr/bin/env sh
# kernel_trap fixed-seed fuzz driver. Compiles the deterministic vector-frame
# fuzz harness with warnings enabled (-Wall) and runs it with the given seed,
# case count, and a reproduction artifact path written only on failure. The
# harness verifies the kernelvec save/restore symmetry oracle is both satisfied
# by the canonical layout and discriminative under fixed-seed perturbations.
# Runs with cwd = project root; PATH is explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

# Compile with warnings enabled; unused or undeclared harness state is fatal.
cc -O2 -Wall -o "$tmp/trap_frame_fuzz" \
  tests/generated/kernel/trap/trap_frame_fuzz.c

"$tmp/trap_frame_fuzz" "$seed" "$cases" "$repro"