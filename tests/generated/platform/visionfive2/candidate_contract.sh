#!/usr/bin/env sh
set -eu
grep -q 'board: StarFive VisionFive 2' spec/design.yaml
grep -q 'acceptance: candidate' spec/modules/platform-visionfive2.yaml
grep -q 'human_review: pending_human_review' spec/modules/platform-visionfive2.yaml
grep -q 'physical four-hart usertests and human review pending' spec/design.yaml
grep -q 'program: python3' vos.yaml
grep -q 'tools/vf2_hardware_runner.py' vos.yaml
grep -q 'review_status": "pending_human_review"' tools/vf2_hardware_runner.py
echo VISIONFIVE2_CANDIDATE_OK
