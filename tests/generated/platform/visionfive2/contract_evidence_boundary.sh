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
  # grep -q may close the pipe before tr has finished; under set -e some
  # shells then report tr's SIGPIPE as a failed contract.  Read the bounded
  # text first so the match result alone determines the check outcome.
  content="$(awk '{$1=$1; printf "%s ", $0}' "$1")"
  case "$content" in
    *"$2"*) ;;
    *) return 1 ;;
  esac
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
has "$readme" 'hardware evidence from `pending_human_review` to `passed`'

# 5. A bounded serial timeout is declared so the evidence finalization is
#    bounded and never promoted.
has "$spec" "bounded serial timeout"

# 6. design.yaml keeps the machine model descriptive, not a hardware claim.
has "$design" "status: candidate"
has "$design" "execution: single boot hart schedules process trees"

echo "contract: QEMU/fixture output stays pending_human_review and is never promoted to a hardware pass"
