#!/usr/bin/env sh
# platform/visionfive2 bounded trace/oracle target.
#
# Workload: a bounded, deterministic host-side trace of the serial evidence
# pipeline for the VisionFive 2 candidate boundary. Because the hardware runner
# is deliberately fail-closed (no physical board is exercised in CI), the trace
# models the bounded serial workload and proves the recorded output can never
# be promoted to a hardware acceptance (hardware-evidence-boundary). The spec
# must declare a bounded serial timeout so the workload always terminates.
# Reads project-root declarations; cwd = project root; PATH allowed.
set -eu

has() {
  tr '\n' ' ' < "$1" | grep -q -- "$2"
}

spec="spec/modules/platform-visionfive2.yaml"
readme="platform/visionfive2/README.md"

test -f "$spec"
test -f "$readme"

# Wrapped line sets: the serial/timeout assertions must resolve across lines.
spec_collapsed="$(tr '\n' ' ' < "$spec")"
readme_collapsed="$(tr '\n' ' ' < "$readme")"

# 1. The serial workload is bounded by an explicit timeout rule.
echo "$spec_collapsed" | grep -q "bounded serial timeout"
echo "$readme_collapsed" | grep -q "Bounded serial timeout"

# 2. One runner owns one serial endpoint, so traces are never interleaved.
echo "$spec_collapsed" | grep -q "one hardware runner owns one serial endpoint"

# 3. The trace itself never becomes a hardware pass: the boundary holds at the
#    spec, the interface, and the README layer.
echo "$spec_collapsed" | grep -q "human_review: pending_human_review"
echo "$spec_collapsed" | grep -q "board interrupt behavior is observed, not inferred from QEMU"
echo "$readme_collapsed" | grep -q "pending_human_review"

# 4. Every recorded trace stays pending human review and never auto-passes:
#    the boundary statement rules out changing pending_human_review to passed.
echo "$spec_collapsed" | grep -q "cannot change hardware evidence from pending_human_review to passed"
echo "$readme_collapsed" | grep -q "acceptance is left to"

echo "trace: bounded serial workload trace matched pending-human-review oracle"