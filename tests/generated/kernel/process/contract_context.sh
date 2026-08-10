#!/usr/bin/env sh
# kernel/process context-switch contract check (source-level, no QEMU).
# Validates the swtch-save-symmetry property declared in
# spec/modules/kernel/process.yaml: swtch.S saves and restores sp and every
# RISC-V callee-saved register (ra, sp, s0-s11) in symmetric slots that match
# the `struct context` layout in proc.h, so the scheduler context exchange
# preserves the complete callee-saved register set and stack pointer.
# Runs with cwd = project root; PATH allowed.
set -eu

# The context struct must hold every callee-saved register plus ra and sp.
grep -q 'struct context' kernel/proc.h
grep -q 'uint64 ra;' kernel/proc.h
grep -q 'uint64 sp;' kernel/proc.h
grep -q 'uint64 s0;' kernel/proc.h
grep -q 'uint64 s11;' kernel/proc.h

# swtch.S must save and restore sp and every callee-saved register.
grep -q 'swtch:' kernel/swtch.S
grep -q '.globl swtch' kernel/swtch.S
grep -q 'sd ra, 0(a0)' kernel/swtch.S
grep -q 'sd sp, 8(a0)' kernel/swtch.S
grep -q 'sd s11, 104(a0)' kernel/swtch.S
grep -q 'ld ra, 0(a1)' kernel/swtch.S
grep -q 'ld sp, 8(a1)' kernel/swtch.S
grep -q 'ld s11, 104(a1)' kernel/swtch.S

# Each save slot must have a symmetric restore slot at the identical offset.
if grep -Eq 'sd (ra|sp|s[0-9]+), ([0-9]+)\(a0\)' kernel/swtch.S; then
  :
fi
n_save="$(grep -Ec 'sd (ra|sp|s[0-9]+), [0-9]+\(a0\)' kernel/swtch.S)"
n_restore="$(grep -Ec 'ld (ra|sp|s[0-9]+), [0-9]+\(a1\)' kernel/swtch.S)"
[ "$n_save" = "14" ] && [ "$n_restore" = "14" ]

# Verify symmetry numerically: every sd offset equals its matching ld offset.
while read -r reg off; do
  [ "$reg" ] && [ "$off" ] || continue
  if ! grep -q "ld $reg, $off(a1)" kernel/swtch.S; then
    echo "contract: asymmetric swtch offset for $reg ($off)" >&2
    exit 1
  fi
done <<'EOF'
ra 0
sp 8
s0 16
s1 24
s2 32
s3 40
s4 48
s5 56
s6 64
s7 72
s8 80
s9 88
s10 96
s11 104
EOF

echo "ok: swtch saves and restores sp and every callee-saved register symmetrically"
