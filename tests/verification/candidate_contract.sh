#!/usr/bin/env sh
set -eu
grep -q '202608121251049-9e35f531' tests/verification/coverage.md
grep -q '202608121158205-35a8ff1d' tests/verification/failure-analysis.md
grep -q 'pending_human_review' tests/verification/coverage.md
grep -q 'physical run unavailable' tests/verification/coverage.md
echo LAB10_CANDIDATE_OK
