#!/usr/bin/env sh
# select_toolchain.sh - probe RISC-V compiler prefixes and echo the first
# one that can compile an empty freestanding RV64 object. The capability
# probe compiles and links against no libc, so command presence alone is
# never sufficient. Shared by the Makefile and the generated tests.
set -eu

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cat >"$tmp/probe.c" <<'EOF'
void toolchain_probe(void) {}
EOF

for p in riscv64-unknown-elf- riscv64-linux-gnu- riscv64-elf-; do
  if command -v "${p}gcc" >/dev/null 2>&1; then
    if "${p}gcc" \
        -march=rv64gc -mabi=lp64 -ffreestanding -fno-builtin \
        -nostdlib -mcmodel=medany -c "$tmp/probe.c" -o "$tmp/probe.o" \
        >/dev/null 2>&1; then
      if command -v "${p}ld" >/dev/null 2>&1 && \
         command -v "${p}objdump" >/dev/null 2>&1; then
        echo "${p}"
        exit 0
      fi
    fi
  fi
done

echo "no capable RISC-V toolchain found" >&2
exit 1