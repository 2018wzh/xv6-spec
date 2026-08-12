#!/usr/bin/env sh
# platform/visionfive2 public check: the hardware runner fails closed and the
# QEMU regression stays separated as an explicit distinct runner.
#
# The ModuleSpec guarantee is "missing inputs or probes never become a
# successful board claim"; the runner therefore exits non-zero (currently 2)
# until a physical board is supplied, and simulated/fixture output never
# produces hardware evidence. Runs with cwd = project root; PATH allowed.
set -eu

has() {
  tr '\n' ' ' < "$1" | grep -q -- "$2"
}

vos="vos.yaml"
test -f "$vos"

# 1. A dedicated hardware runner exists and is fail-closed with exit 2.
has "$vos" "hardware:"
has "$vos" "exit 2"
has "$vos" "program: sh"

# 2. The QEMU regression runner is a separate, distinct runner whose success
#    pattern only proves boot, never hardware acceptance.
has "$vos" "qemu:"
has "$vos" "success_pattern: XV6_BOOT_OK"

# 3. The README explicitly states the runner makes no hardware claim without
#    a physical board + serial endpoint + immutable workload.
has platform/visionfive2/README.md "fail-closed"
has platform/visionfive2/README.md "nobody claims a passed hardware result"

echo "public: hardware runner fails closed and is distinct from the QEMU boot regression"