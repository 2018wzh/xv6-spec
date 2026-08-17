#!/usr/bin/env sh
# platform/visionfive2 fixed-seed hash-record fuzz driver. Compiles the
# deterministic host model (hash_scheme_fuzz.c) with `cc` and runs it with the
# given seed, case count, and a reproduction artifact path written only on
# failure. The harness validates evidence determinism for immutable board input
# hashes and that a modified input fails closed (algorithm_intent /
# validate_board_inputs.post). Runs with cwd = project root; PATH allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

cc -O2 -Wall -Wextra -Werror \
  -o "$tmp/hash_scheme_fuzz" \
  tests/generated/platform/visionfive2/hash_scheme_fuzz.c

"$tmp/hash_scheme_fuzz" "$seed" "$cases" "$repro"