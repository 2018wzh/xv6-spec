#!/usr/bin/env sh
# toolchain fixed-seed mkfs-layout fuzz driver (toolchain_mkfs_layout_fuzz).
#
# Compiles the deterministic host-model harness with `cc` and runs it with
# the given seed, case count, and a reproduction-artifact path written only
# on failure. The harness fuzzes the mkfs in-memory image layout to confirm
# the toolchain/inode disk-layout-partition invariants hold under every
# reachable geometry: regions are in range, non-overlapping, and cover the
# declared logical blocks. It is a concrete, deterministic fixed-seed model
# of the image layout (the real image is produced by mkfs/mkfs and booted by
# the inode public check).
#
# Runs with cwd = project root; PATH is explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

dir="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -O2 -Wall -o "$tmp/mkfs_layout_fuzz" "$dir/mkfs_layout_fuzz.c"

"$tmp/mkfs_layout_fuzz" "$seed" "$cases" "$repro"
