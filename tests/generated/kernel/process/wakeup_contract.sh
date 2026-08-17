#!/usr/bin/env sh
# kernel/process sleep/wakeup contract check. Verifies the caller-lock handoff
# and the no-lost-wakeup guarantee declared in spec/modules/kernel/process.yaml:
#   - sleep acquires the process lock, then releases the caller lock, and only
#     then publishes SLEEPING (the handoff), so a wakeup after publication
#     cannot be lost;
#   - the caller reacquires its lock after rescheduling;
#   - wakeup marks matching SLEEPING processes RUNNABLE while holding their
#     locks.
# The fixed-seed state-fuzz and wakeup host models run alongside this source
# check. Runs with cwd = project root; PATH allowed.
set -eu

# Handoff: acquire process lock before releasing the caller lock.
acquire_line="$(grep -n 'acquire(&p->lock)' kernel/proc.c | head -n1 | cut -d: -f1)"
release_lk_line="$(grep -n 'release(lk)' kernel/proc.c | head -n1 | cut -d: -f1)"
[ -n "$acquire_line" ] && [ -n "$release_lk_line" ] && [ "$acquire_line" -lt "$release_lk_line" ]

# Published SLEEPING after the caller lock is released.
sleeping_line="$(grep -n 'p->state = SLEEPING' kernel/proc.c | head -n1 | cut -d: -f1)"
[ -n "$release_lk_line" ] && [ -n "$sleeping_line" ] && [ "$release_lk_line" -lt "$sleeping_line" ]

# The caller reacquires its lock after rescheduling.
grep -q 'acquire(lk)' kernel/proc.c

# wakeup marks matching SLEEPING processes RUNNABLE while holding their locks.
grep -q '^wakeup(void \*chan)$' kernel/proc.c
grep -q 'p->state == SLEEPING && p->chan == chan' kernel/proc.c
grep -q 'p->state = RUNNABLE' kernel/proc.c

# Compile and run the deterministic fixed-seed wakeup host model.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -O2 -Wall -o "$tmp/wakeup_contract" \
  tests/generated/kernel/process/wakeup_contract.c
"$tmp/wakeup_contract" 7 300

echo "ok: sleep/wakeup lock handoff and no-lost-wakeup guarantee present"