#!/usr/bin/env sh
# rebuild_test.sh - toolchain_clean_rebuild: perform two clean builds under
# the unchanged PATH allowlist and require the same selected prefix and the
# same kernel/kernel content hash across both.
set -eu

select_riscv_toolchain() {
  sh tests/generated/toolchain/select_toolchain.sh
}

prefix_a="$(select_riscv_toolchain)"

make clean >/dev/null
make >/dev/null
hash_a="$(sha256sum kernel/kernel | cut -d' ' -f1)"

make clean >/dev/null
make >/dev/null
hash_b="$(sha256sum kernel/kernel | cut -d' ' -f1)"

# A second capability selection must yield the same prefix and hash.
prefix_b="$(select_riscv_toolchain)"

if [ "$prefix_a" != "$prefix_b" ]; then
  echo "toolchain_clean_rebuild: prefix changed between builds: $prefix_a vs $prefix_b" >&2
  exit 1
fi
if [ "$hash_a" != "$hash_b" ]; then
  echo "toolchain_clean_rebuild: kernel/kernel hash changed across clean builds" >&2
  exit 1
fi

echo "toolchain_clean_rebuild: stable prefix $prefix_a and kernel/kernel hash $hash_a"