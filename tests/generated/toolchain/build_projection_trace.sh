#!/usr/bin/env bash
# toolchain build-projection bounded trace/oracle (toolchain_build_projection_trace).
#
# A bounded trace over the Makefile build projection: it performs one bounded,
# ordered sequence of build invocations and confirms each step reaches its
# declared artifact (a trace with an oracle). The oracle requires:
#   - a clean build produces kernel/kernel and the Lab 1 readers,
#   - the selected RISC-V prefix is stable across the trace, and
#   - a second clean rebuild reproduces the same kernel/kernel content hash.
#
# This targets toolchain.toolchain-deterministic and build_ctf_readers without
# re-implementing the shared framework (it reuses tests/public framework helpers
# where present). Runs with cwd = project root; PATH is explicitly allowed.
set -eu

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

select_riscv_toolchain() {
  sh tests/generated/toolchain/select_toolchain.sh
}

# Trace step 1: select the toolchain prefix.
prefix_before="$(select_riscv_toolchain)"
[ -n "$prefix_before" ] || { echo "trace: no capable RISC-V toolchain" >&2; exit 1; }

# Trace step 2: clean rebuild, expect kernel/kernel + Lab 1 readers + fs.img.
make clean >/dev/null
make >/dev/null
[ -f kernel/kernel ] || { echo "trace: kernel/kernel not produced" >&2; exit 1; }
make lab1/build/flag-reader lab1/build/ctf-baremetal.elf >/dev/null
[ -f lab1/build/flag-reader ]       || { echo "trace: flag-reader missing" >&2; exit 1; }
[ -f lab1/build/ctf-baremetal.elf ] || { echo "trace: ctf-baremetal.elf missing" >&2; exit 1; }

hash_a="$(sha256sum kernel/kernel | cut -d' ' -f1)"

# Trace step 3: a second clean rebuild reproduces the same prefix + hash.
make clean >/dev/null
make >/dev/null
hash_b="$(sha256sum kernel/kernel | cut -d' ' -f1)"
prefix_after="$(select_riscv_toolchain)"

[ "$prefix_before" = "$prefix_after" ] || {
  echo "trace: toolchain prefix changed across builds: $prefix_before vs $prefix_after" >&2
  exit 1
}
[ "$hash_a" = "$hash_b" ] || {
  echo "trace: kernel/kernel hash changed across clean builds" >&2
  exit 1
}

# Trace step 4 (bounded init image trace): fs.img builds from the host mkfs.
make fs.img >/dev/null
[ -f fs.img ] || { echo "trace: fs.img not produced" >&2; exit 1; }

# The make clean must not remove tracked spec/source files (build outputs are
# disposable and never replace tracked sources).
[ -f Makefile ] && [ -f vos.yaml ] && [ -f spec/modules/toolchain.yaml ]

echo "toolchain_build_projection_trace: clean rebuild trace consistent ($prefix_before, hash ${hash_a:0:8})"
