#!/usr/bin/env sh
# kernel/file contract check (kernel_file_reference_contract).
#
# Builds the deterministic root image with the real mkfs tool and compiles the
# real kernel/file.c + kernel/sysfile.c together with the real kernel/fs.c +
# kernel/bio.c + kernel/log.c against the deterministic harness (file_test.c,
# contract mode). Exercises the kernel/file invariants:
#   - descriptor-reference-consistency: one ref per populated slot; close
#     clears the slot before final release.
#   - file-offset-serialization: offsets advance by exactly the transferred
#     byte count; dup observes one serialized shared offset.
#   - close-release-once: final close releases the inode-backed resource once.
#   - user-buffer-validation and namespace_mutation (create/link/unlink/
#     mkdir/chdir publish only committed namespace state).
# Runs with cwd = project root; PATH explicitly allowed.
set -eu
dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -O2 -Wall -Wextra -Werror -I kernel -o "$tmp/mkfs" mkfs/mkfs.c
"$tmp/mkfs" "$tmp/fs.img" >/dev/null

cc -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel \
   -o "$tmp/file_test" "$dir/file_test.c" \
   kernel/fs.c kernel/bio.c kernel/log.c kernel/file.c kernel/sysfile.c

if ! "$tmp/file_test" "$tmp/fs.img"; then
  echo "kernel_file_reference_contract: contract harness failed" >&2
  exit 1
fi

echo "kernel_file_reference_contract: descriptor and inode reference invariants hold"
