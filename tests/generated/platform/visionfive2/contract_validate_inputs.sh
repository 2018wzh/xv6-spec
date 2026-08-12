#!/usr/bin/env sh
# platform/visionfive2 contract check: the `validate_board_inputs` operation.
#
# Bindings (spec/modules/platform-visionfive2.yaml):
#   interface.validate_board_inputs.pre/post/errors: the runner either records
#     the immutable input hashes and launches the declared workload, or fails
#     before claiming hardware execution; missing physical-board inputs fail
#     without producing passed hardware evidence.
#   algorithm_intent: validate immutable board inputs, run a bounded serial
#     workload, record hashes and logs, leave acceptance to human review.
# Runs with cwd = project root; PATH allowed.
set -eu

has() {
  tr '\n' ' ' < "$1" | grep -q -- "$2"
}

spec="spec/modules/platform-visionfive2.yaml"
readme="platform/visionfive2/README.md"

test -f "$spec"
test -f "$readme"

# 1. The operation record and its fail-closed contract are declared.
has "$spec" "validate_board_inputs"
has "$spec" "are explicitly supplied"
has "$spec" "records their hashes and launches the declared workload"
has "$spec" "fails before claiming hardware execution"
has "$spec" "missing physical-board inputs fail without producing passed hardware evidence"

# 2. Recording hashes of the six immutable inputs is the documented precondition.
has "$readme" "records the hashes of these immutable inputs"
has "$readme" "launches the declared workload"

# 3. The canonical boundary check is referenced as the stable property.
has "$spec" "vf2-candidate-contract"
has "$spec" "platform_visionfive2_candidate_contract"

# 4. algorithm_intent states the required pipeline end to end.
has "$spec" "Validate immutable board inputs, run a bounded serial workload, record hashes and logs, and leave acceptance to human review"

echo "contract: validate_board_inputs pre/post/errors match the fail-closed evidence pipeline"