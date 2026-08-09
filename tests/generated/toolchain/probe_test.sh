#!/usr/bin/env sh
# probe_test.sh - toolchain_capability_probe: verify that a capable RISC-V
# prefix is selected and can compile an empty freestanding RV64 object.
# Runs with cwd set to the project root and PATH explicitly allowed.
set -eu

prefix="$(sh tests/generated/toolchain/select_toolchain.sh)"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cat >"$tmp/probe.c" <<'EOF'
void toolchain_probe(void) {}
EOF

"${prefix}gcc" \
  -march=rv64gc -mabi=lp64 -ffreestanding -fno-builtin \
  -nostdlib -mcmodel=medany -c "$tmp/probe.c" -o "$tmp/probe.o"

"${prefix}ld" -z max-page-size=4096 -o "$tmp/probe.elf" "$tmp/probe.o"

"${prefix}objdump" -h "$tmp/probe.elf" >/dev/null

echo "toolchain_capability_probe: selected prefix ${prefix} compiled a freestanding RV64 object"