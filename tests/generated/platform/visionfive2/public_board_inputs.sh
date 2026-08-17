#!/usr/bin/env sh
# platform/visionfive2 public check: required board inputs + candidate boundary.
#
# Binds to the ModuleSpec `validate_board_inputs` operation and the candidate
# boundary declared in spec/modules/platform-visionfive2.yaml: board identity,
# firmware, DTB, image, serial device, and workload must all be explicit before
# hardware execution can be (at most) pending_human_review. Runs with cwd =
# project root; PATH explicitly allowed.
set -eu

# `has FILE PATTERN` greps on a newline-collapsed copy of FILE so multi-word
# phrases survive source-line wrapping without losing the required word
# separation (normalization maps each newline to a single space).
has() {
  tr '\n' ' ' < "$1" | grep -q -- "$2"
}

spec="spec/modules/platform-visionfive2.yaml"
readme="platform/visionfive2/README.md"
design="spec/design.yaml"

test -f "$spec"
test -f "$readme"
test -f "$design"

# 1. The operator must supply all six physical board inputs explicitly.
for input in \
  "board identity" \
  "firmware" \
  "DTB" \
  "image" \
  "serial device" \
  "workload"; do
  has "$spec" "$input"
  has "$readme" "$input"
done
# The module pre-condition names the full comma-separated input set.
has "$spec" "board identity, firmware, DTB, image, serial device, and workload are explicitly supplied"

# 2. Acceptance is candidate / pending human review, not passed hardware.
has "$spec" "acceptance: candidate"
has "$spec" "human_review: pending_human_review"
has "$design" "board: StarFive VisionFive 2"
has "$design" "physical four-hart usertests and human review pending"

# 3. The board is documented as the physical VisionFive 2, not a QEMU model.
has "$readme" "StarFive VisionFive 2"
has "$readme" "four U74 application harts"

echo "public: all six operator-supplied board inputs and the candidate boundary are declared"