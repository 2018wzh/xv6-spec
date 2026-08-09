#!/usr/bin/env sh
# kernel_boot PMP-before-mret contract check.
#
# lab2-supervisor-pmp-access requires that machine mode writes the maximal
# pmpaddr0 NAPOT region and pmpcfg0 read-write-execute permissions BEFORE
# mret, and that MPP is set to supervisor so mret lands in supervisor mode
# with the linked image accessible. This source-level contract checks the
# ordering in kernel/start.c and the boot_banner entry point in kernel/boot.c.
set -eu

# 1. PMP configuration CSR writes are present in start.c.
grep -q 'w_pmpaddr0' kernel/start.c
grep -q 'w_pmpcfg0' kernel/start.c

# 2. pmpcfg0 grants R, W, X and uses NAPOT addressing.
grep -q 'w_pmpcfg0(PMP_R | PMP_W | PMP_X | PMP_A_NAPOT)' kernel/start.c

# 3. MPP is set to supervisor before mret.
grep -q 'MSTATUS_MPP' kernel/start.c
grep -q 'MSTATUS_MPP_S' kernel/start.c
grep -q 'mret()' kernel/start.c

# 4. PMP configuration happens before the mret that leaves machine mode.
pmp_line="$(grep -n 'w_pmpaddr0\|w_pmpcfg0' kernel/start.c | head -n1 | cut -d: -f1)"
mret_line="$(grep -n 'mret()' kernel/start.c | head -n1 | cut -d: -f1)"
test -n "$pmp_line" && test -n "$mret_line" && [ "$pmp_line" -lt "$mret_line" ]

# 5. The boot_banner entry point returns an immutable XV6_BOOT_OK banner.
grep -q 'const char \*' kernel/boot.c
grep -q 'boot_banner(void)' kernel/boot.c
grep -q 'XV6_BOOT_OK' kernel/boot.c

# 6. riscv.h exposes the correct pmpcfg0/pmpaddr0 CSR encodings (RV64) and
#    does not confuse pmpaddr0 with pmpcfg3 (0x3a3).
grep -q 'CSRPMPADDR0 0x3B0' kernel/riscv.h
grep -q 'CSRPMPCFG0  0x3A0' kernel/riscv.h

echo "contract: supervisor PMP configured before mret; boot_banner present"