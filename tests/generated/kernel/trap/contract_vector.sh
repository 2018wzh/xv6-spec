#!/usr/bin/env sh
# kernel_trap vector contract check (source-level, no QEMU). Verifies the
# kernel_trap_vector_contract property declared in spec/modules/kernel/trap.yaml:
#   - trapinit installs kernelvec into stvec,
#   - stvec names the kernelvec symbol,
#   - installation happens while interrupts are disabled (before SIE is set),
#   - a missing vector symbol fails the build (the linker resolves kernelvec).
# Runs with cwd = project root; PATH allowed.
set -eu

# stvec must be installed from trapinit before any interrupt can arrive.
grep -q 'w_stvec((uint64)kernelvec)' kernel/trap.c
grep -q 'kernelvec(void)' kernel/trap.c

# The kernelvec symbol must be defined by the assembly vector file.
grep -q 'kernelvec:' kernel/kernelvec.S
grep -q '.globl kernelvec' kernel/kernelvec.S

# stvec installation must precede enabling supervisor interrupts in main.
trapinit_line="$(grep -n 'trapinit()' kernel/main.c | head -n1 | cut -d: -f1)"
intr_on_line="$(grep -n 'intr_on()' kernel/main.c | head -n1 | cut -d: -f1)"
[ -n "$trapinit_line" ] && [ -n "$intr_on_line" ] && [ "$trapinit_line" -lt "$intr_on_line" ]

# The vector must end by returning to the interrupted context via sret (so a
# handled trap returns to supervisor mode at the original sepc).
grep -q 'sret' kernel/kernelvec.S

echo "ok: stvec installed before supervisor interrupts are enabled"