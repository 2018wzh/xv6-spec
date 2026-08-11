#!/usr/bin/env sh
# kernel_boot public: deterministic early-entry stack check. Binds the
# owned_state "one early stack for the boot hart" by verifying that
# kernel.ld allocates exactly one boot stack (bootstack->bootstacktop) sized
# for a single hart, and that entry.S loads that stack top into sp before
# dispatching to start(). Runs with cwd = project root; PATH allowed.
set -eu

grep -q 'bootstack = \.' kernel/kernel.ld
grep -q 'bootstacktop = \.' kernel/kernel.ld
grep -q 'bootstacktop\[\]' kernel/memlayout.h
# entry.S must point the early stack pointer at the linked bootstacktop.
grep -q 'la sp, bootstacktop' kernel/entry.S
grep -q 'call start' kernel/entry.S

echo "public: single early boot stack configured and entered by entry.S"
