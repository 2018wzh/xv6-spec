#!/usr/bin/env sh
set -eu
out="$(mktemp)"
trap 'rm -f "$out"' EXIT HUP INT TERM
timeout 15 make qemu >"$out" 2>&1 || test "$?" -eq 124
test "$(grep -c '^XV6_BOOT_OK$' "$out")" -eq 1
! grep -q 'panic' "$out"
grep -q 'sys_fork' kernel/syscall.c
grep -q 'sys_exec' kernel/syscall.c
grep -q 'sys_wait' kernel/syscall.c
echo PROCESS_TREE_TRACE_OK
