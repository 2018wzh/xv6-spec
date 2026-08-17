#!/usr/bin/env sh
# kernel/file fixed-seed fuzz driver (kernel_file_abi_fuzz).
#
# Builds the deterministic mkfs root image, compiles the real kernel/file.c +
# kernel/sysfile.c together with the real kernel/fs.c + kernel/bio.c +
# kernel/log.c against the deterministic single-threaded harness
# (file_test.c) and runs the fixed-seed descriptor/offset/append/close cycle
# workload, preserving the file_capacity and read_write invariants. Persists
# a deterministic reproduction artifact. Runs with cwd = project root; PATH
# explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" "$tmp/fs.img" >/dev/null

cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/file_test" "$dir/file_test.c" \
   kernel/fs.c kernel/bio.c kernel/log.c kernel/file.c kernel/sysfile.c

if ! "$tmp/file_test" "$tmp/fs.img" "$seed" "$cases"; then
  echo "kernel_file_abi_fuzz: fuzz workload failed" >&2
  exit 1
fi

printf 'kernel_file_abi_fuzz seed=%s cases=%s\n' "$seed" "$cases" > "$repro"
echo "kernel_file_abi_fuzz: fixed-seed fuzz passed"
