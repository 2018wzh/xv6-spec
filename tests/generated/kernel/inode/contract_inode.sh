#!/usr/bin/env sh
# kernel/inode contract check. Builds the deterministic root image with the
# real mkfs tool, then compiles the real kernel/fs.c together with the real
# kernel/bio.c and kernel/log.c plus the deterministic single-threaded harness
# (fs_test.c) and runs the contract mode (no seed) that exercises fsinit /
# inode_block_lifecycle / path_resolution against the kernel/inode invariants:
#   - filesystem-admission-order: fsinit validates geometry, completes redo-log
#     recovery, and mounts the deterministic image before persistent operations.
#   - allocation-reference-consistency: allocate/truncate/unlink/recreate cycles
#     restore free counts; freed inumes are reusable.
#   - inode-cache-identity / path-lock-progress: dirlookup/dirlink/unlink and
#     component-wise traversal (namei, nameiparent, dot/dot-dot) preserve
#     directory references and release the parent before waiting on a child.
#   - committed metadata survives a second fsinit (remount).
# Runs with cwd = project root; PATH explicitly allowed.
set -eu
dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Build the deterministic mkfs image generator and produce fs.img in a temp.
cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" "$tmp/fs.img" >/dev/null

# Compile the real kernel/fs.c, kernel/bio.c, kernel/log.c with the harness.
# -ffreestanding/-fno-builtin match the kernel build flags and avoid host
# builtin declaration conflicts with defs.h.
cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/fs_test" "$dir/fs_test.c" kernel/fs.c kernel/bio.c kernel/log.c

if ! "$tmp/fs_test" "$tmp/fs.img"; then
  echo "kernel/inode contract failed" >&2
  exit 1
fi