#!/usr/bin/env sh
# Lab 7 toolchain diagnostic contract: invalid assembly must fail closed and
# retain the compiler's original diagnostic instead of becoming a generic VOS
# success or a silently accepted artifact.
set -eu

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
prefix="$(sh tests/generated/toolchain/select_toolchain.sh)"
cc="${prefix}gcc"

cat >"$tmp/invalid.S" <<'EOF'
.section .text,"ax"
.globl _invalid_contract_entry
_invalid_contract_entry:
  .this_directive_must_fail
EOF

if "$cc" -march=rv64gc -mabi=lp64 -ffreestanding -c "$tmp/invalid.S" -o "$tmp/invalid.o" 2>"$tmp/diagnostic"; then
  echo "contract: invalid assembly unexpectedly succeeded" >&2
  exit 1
fi
[ -s "$tmp/diagnostic" ] || {
  echo "contract: compiler failed without its original diagnostic" >&2
  exit 1
}
[ ! -e "$tmp/invalid.o" ] || {
  echo "contract: failed compile published an object" >&2
  exit 1
}

echo "toolchain_linker_diagnostic_contract: invalid input failed with compiler diagnostic"
