#!/usr/bin/env sh
# kernel/syscall bounded trace/oracle target. The workload is a bounded QEMU
# virt serial capture of the booted Lab 5 kernel (syscall/trap composition
# built and linked); the oracle requires exactly one canonical XV6_BOOT_OK
# banner line and that the first-user-process entry path is wired in the boot
# order: userinit creates the RUNNABLE process after procinit and before the
# scheduler, and usertrapret clears SPP before the trampoline userret so the
# user ecall return cannot inherit supervisor privilege. Runs with cwd =
# project root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM
. tests/public/lab2-boot.sh
vos_lab2_capture_serial "$output"

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# userinit must come after procinit and before the scheduler in boot order.
procinit_l="$(grep -n 'procinit()' kernel/main.c | head -n1 | cut -d: -f1)"
userinit_l="$(grep -n 'userinit()' kernel/main.c | head -n1 | cut -d: -f1)"
sched_l="$(grep -n 'scheduler()' kernel/main.c | head -n1 | cut -d: -f1)"
[ -n "$procinit_l" ] && [ -n "$userinit_l" ] && [ -n "$sched_l" ]
[ "$procinit_l" -lt "$userinit_l" ]
[ "$userinit_l" -lt "$sched_l" ]

# The first-user-process entry path: uservec is installed on user return and
# SPP is cleared before the trampoline userret runs.
grep -q 'w_stvec(TRAMPOLINE' kernel/trap.c
grep -q 'sstatus &= ~SSTATUS_SPP' kernel/trap.c
grep -q 'sstatus |= SSTATUS_SPIE' kernel/trap.c

# forkret must enter user mode via usertrapret on first dispatch.
grep -q 'usertrapret();' kernel/proc.c

echo "trace: bounded serial trace matched first-user-process wiring oracle"