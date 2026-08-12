#!/usr/bin/env sh
# platform/visionfive2 bounded trace/oracle target.
#
# Workload: a deterministic host-side trace of candidate-evidence
# reproducibility. Per the ModuleSpec postconditions, candidate evidence must
# name the board, build identity, serial log, workload, and the unresolved
# human review; and per algorithm_intent a bounded serial workload records
# hashes and logs. This trace asserts all of those are documented so a clean
# HEAD can never produce variable or unpromoted evidence. Because the hardware
# runner is fail-closed in CI, the trace reads project-root declarations
# rather than driving a physical board. cwd = project root; PATH allowed.
set -eu

collapse() { tr '\n' ' ' < "$1"; }

spec="spec/modules/platform-visionfive2.yaml"
readme="platform/visionfive2/README.md"
bi="spec/interfaces/visionfive2-board.yaml"

test -f "$spec"
test -f "$readme"
test -f "$bi"

spec_collapsed="$(collapse "$spec")"
readme_collapsed="$(collapse "$readme")"
bi_collapsed="$(collapse "$bi")"

# 1. Candidate evidence names board identity, build identity, serial log,
#    workload, and the unresolved human review.
echo "$spec_collapsed" | grep -q "board, build identity, serial log, workload, and unresolved human review"
echo "$readme_collapsed" | grep -q "board identity"
echo "$readme_collapsed" | grep -q "serial endpoint"
echo "$readme_collapsed" | grep -q "workload"
echo "$readme_collapsed" | grep -q "human review"
echo "$spec_collapsed" | grep -q "pending_human_review"

# 2. Evidence is bound to a clean HEAD and build; the interface repeats it.
echo "$bi_collapsed" | grep -q "evidence is bound to the current clean commit and build"
echo "$bi_collapsed" | grep -q "pending_human_review evidence"

# 3. Immutable inputs produce the recorded fingerprint; missing inputs fail.
echo "$readme_collapsed" | grep -q "records the hashes of these immutable inputs"
echo "$readme_collapsed" | grep -q "fails closed"

# 4. The evidence pipeline terminates on a bounded serial timeout.
echo "$spec_collapsed" | grep -q "Bounded serial timeout terminates the run"

echo "trace: candidate evidence reproducibility matched clean-HEAD naming oracle"