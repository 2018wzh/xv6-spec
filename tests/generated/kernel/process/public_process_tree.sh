#!/usr/bin/env sh
set -eu
grep -q '^fork(void)' kernel/proc.c
grep -q '^wait(uint64 addr)' kernel/proc.c
grep -q '^exit(int status)' kernel/proc.c
grep -q '^exec(char \*path, char \*\*argv)' kernel/exec.c
make kernel/kernel >/dev/null
echo PROCESS_TREE_PUBLIC_OK
