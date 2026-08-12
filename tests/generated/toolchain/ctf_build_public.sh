#!/usr/bin/env sh
# toolchain.build_ctf_readers public check (toolchain_build_readers_public).
#
# Verifies the Lab 1 CTF reader build projection owned by kernel/toolchain:
#   - both declared reader artifacts build from tracked sources through the
#     Makefile (the Linux reader and the freestanding QEMU image),
#   - the deterministic non-secret fixture image and metadata are generated,
#   - the toolchain prefix selection is recorded for the build.
#
# Runs with cwd = project root; PATH is explicitly allowed so make and the
# RISC-V compiler resolve the same host tools.
set -eu

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Owned source files for both readers must exist; a missing source is a
# failing public check, not a skipped one.
[ -f lab1/linux/flag-reader.c ] || { echo "public: missing lab1/linux/flag-reader.c" >&2; exit 1; }
[ -f lab1/baremetal/main.c ]   || { echo "public: missing lab1/baremetal/main.c" >&2; exit 1; }
[ -f Makefile ]                || { echo "public: missing Makefile" >&2; exit 1; }

# Both reader artifacts build from tracked sources via the Makefile argv
# projection (program + arguments remain separate fields; cwd is the repo).
make lab1/build/flag-reader lab1/build/ctf-baremetal.elf >/dev/null

[ -x lab1/build/flag-reader ]      || { echo "public: flag-reader not produced" >&2; exit 1; }
[ -f lab1/build/ctf-baremetal.elf ] || { echo "public: ctf-baremetal.elf not produced" >&2; exit 1; }

# The deterministic non-secret fixture (flags.img + metadata.json) is
# generated as part of the baremetal artifact rule.
[ -f lab1/build/fixture/flags.img ]     || { echo "public: fixture flags.img missing" >&2; exit 1; }
[ -f lab1/build/fixture/metadata.json ] || { echo "public: fixture metadata.json missing" >&2; exit 1; }

# The capable RISC-V tool prefix must be reproducible on this PATH.
tool_prefix="$(sh tests/generated/toolchain/select_toolchain.sh)"
[ -n "$tool_prefix" ] || { echo "public: no capable RISC-V toolchain selected" >&2; exit 1; }
case "$tool_prefix" in
  *) echo "toolchain_build_readers_public: built Lab 1 readers with prefix $tool_prefix" ;;
esac
