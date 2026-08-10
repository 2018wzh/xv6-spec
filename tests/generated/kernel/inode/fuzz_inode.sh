#!/usr/bin/env sh
# kernel/inode fixed-seed fuzz driver. Builds the deterministic mkfs root
# image, compiles the real kernel/fs.c + kernel/bio.c + kernel/log.c against
# the deterministic single-threaded harness (fs_test.c) with host cc, and runs
# the fixed-seed allocate/truncate/unlink/recreate and directory-mutation
# workload. The harness mirrors the kernel/inode invariants
# (allocation-reference-consistency, inode-cache-identity, path-lock-progress);
# panics are intercepted so expected validation-error paths run without
# aborting. Runs with cwd = project root; PATH explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$(dirname "$repro")"

# Build mkfs and generate a fresh deterministic image.
cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" "$tmp/fs.img" >/dev/null

# Compile the real kernel/fs.c, kernel/bio.c, kernel/log.c with the harness.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/fs_test" "$dir/fs_test.c" kernel/fs.c kernel/bio.c kernel/log.c

if ! "$tmp/fs_test" "$tmp/fs.img" "$seed" "$cases"; then
  echo "kernel/inode fuzz failed" >&2
  exit 1
fi

# Persist a deterministic reproduction artifact for this fixed seed.
printf 'kernel_inode_fuzz seed=%s cases=%s\n' "$seed" "$cases" > "$repro"