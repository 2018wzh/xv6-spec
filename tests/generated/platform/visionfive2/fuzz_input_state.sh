#!/usr/bin/env sh
# platform/visionfive2 fixed-seed input-completeness fuzz driver. Compiles the
# deterministic host model (input_state_fuzz.c) with `cc` and runs it with the
# given seed, case count, and a reproduction artifact path written only on
# failure. The harness validates the fail-closed gate over random subsets of
# the six required board inputs (validate_board_inputs; no-simulated-hardware-
# pass). Runs with cwd = project root; PATH explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

cc -O2 -Wall -Wextra -Werror \
  -o "$tmp/input_state_fuzz" \
  tests/generated/platform/visionfive2/input_state_fuzz.c

"$tmp/input_state_fuzz" "$seed" "$cases" "$repro"