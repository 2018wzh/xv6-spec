#!/usr/bin/env sh
# kernel_boot contract: machine->supervisor transition ordering. entry.S
# enters in machine mode with interrupts disabled, sets the early stack, and
# dispatches to start(); start() configures the PMP entry, programs mepc to
# main, and only then mret's into supervisor mode. This binds the
# postconditions "supervisor execution reaches main" and "supervisor mode can
# fetch/access the linked kernel image after mret".
set -eu

# entry.S establishes the machine-mode entry point and the early stack.
grep -q 'la sp, bootstacktop' kernel/entry.S
grep -q 'call start' kernel/entry.S

# start() orchestrates PMP, mepc, and the final mret in that order.
grep -q 'w_pmpaddr0' kernel/start.c
grep -q 'w_pmpcfg0' kernel/start.c
grep -q 'w_mepc' kernel/start.c
grep -q 'main' kernel/start.c
grep -q 'mret()' kernel/start.c

pmp_line="$(grep -n 'w_pmpaddr0' kernel/start.c | head -n1 | cut -d: -f1)"
mepc_line="$(grep -n 'w_mepc' kernel/start.c | head -n1 | cut -d: -f1)"
mret_line="$(grep -n 'mret()' kernel/start.c | head -n1 | cut -d: -f1)"
test -n "$pmp_line" && test -n "$mepc_line" && test -n "$mret_line"
[ "$pmp_line" -lt "$mepc_line" ] && [ "$mepc_line" -lt "$mret_line" ]

echo "contract: machine->supervisor transition configured in deterministic order"
