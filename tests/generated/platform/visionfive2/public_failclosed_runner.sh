#!/usr/bin/env sh
# platform/visionfive2 public check: the hardware runner is wired into the
# structured projection and remains fail-closed. Automatic hardware evidence
# is pending_human_review and nobody claims a passed hardware result without
# physical four-hart usertests plus human review.
set -eu

has() {
  tr '\n' ' ' < "$1" | grep -q -- "$2"
}

vos="vos.yaml"
readme="platform/visionfive2/README.md"
runner="tools/vf2_hardware_runner.py"

has "$vos" "hardware:"
has "$vos" "program: python3"
has "$vos" "tools/vf2_hardware_runner.py"
has "$vos" "VOS_VF2_SERIAL_PORT"
has "$vos" "VOS_VF2_BOARD_ALIAS"
has "$vos" "four-hart-usertests"

has "$vos" "qemu:"
has "$vos" "success_pattern: XV6_BOOT_OK"

has "$readme" "fail-closed"
has "$readme" "nobody claims a passed hardware result"
has "$runner" "pending_human_review"
has "$runner" "git"
has "$runner" "sha256"

echo "public: hardware runner is wired, fail-closed, and never auto-passes"
