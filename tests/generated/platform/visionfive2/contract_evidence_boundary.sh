#!/usr/bin/env sh
# platform/visionfive2 contract check: the hardware-evidence boundary and the
# no-simulated-hardware-pass invariant.
#
# Bindings (spec/modules/platform-visionfive2.yaml):
#   properties.hardware-evidence-boundary: local QEMU, fixture serial, and
#     image inspection cannot change hardware evidence from pending_human_review
#     to passed.
#   invariants.no-simulated-hardware-pass: the candidate check rejects any
#     automatic passed hardware conclusion.
#   concurrency.forbidden_patterns: fixture output promoted to hardware
#     acceptance.
#   guarantee: automatic evidence remains pending_human_review.
# Runs with cwd = project root; PATH allowed.
set -eu

has() {
  tr '\n' ' ' < "$1" | grep -q -- "$2"
}

spec="spec/modules/platform-visionfive2.yaml"
readme="platform/visionfive2/README.md"
design="spec/design.yaml"
iface="spec/interfaces/visionfive2-board.yaml"

test -f "$spec"
test -f "$readme"
test -f "$design"
test -f "$iface"

# 1. QEMU is not hardware evidence: the module declares the boundary property.
has "$spec" "hardware-evidence-boundary"
has "$spec" "cannot change hardware evidence from pending_human_review to passed"

# 2. The no-simulated-hardware-pass invariant rejects automatic passes.
has "$spec" "no-simulated-hardware-pass"
has "$spec" "rejects any automatic passed hardware conclusion"

# 3. The interface separates QEMU boot regression from hardware human review.
has "$iface" "four-hart usertests require physical execution and separate human review"

# 4. The README documents the forbidden promotion and automatic pending status.
has "$readme" "pending_human_review"
has "$readme" "cannot change hardware evidence from pending_human_review to passed"

# 5. A bounded serial timeout is declared so the evidence finalization is
#    bounded and never promoted.
has "$spec" "bounded serial timeout"

# 6. design.yaml keeps the machine model descriptive, not a hardware claim.
has "$design" "status: candidate"
has "$design" "execution: single boot hart schedules process trees"

echo "contract: QEMU/fixture output stays pending_human_review and is never promoted to a hardware pass"