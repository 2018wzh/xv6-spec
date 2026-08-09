#!/usr/bin/env sh
# kernel_trap hidden: vector-frame-symmetry source check. Directly parses
# kernel/kernelvec.S and verifies that every register kernelvec saves into a
# stack slot is restored from the identical slot offset (save offset ==
# restore offset, 8-byte slots over a 256-byte frame). This binds the
# symmetry oracle to the exact assembly vector, complementing the model-level
# fuzz harness. It uses only POSIX tools; PATH is explicitly allowed.
set -eu

sf="$PWD/kernel/kernelvec.S"
[ -f "$sf" ]

# Extract "sd <reg>, <off>(sp)" save offsets and "<off>(sp)" restore offsets.
# A register list that must be both saved and restored in kernelvec.
regs="ra gp tp t0 t1 t2 s0 s1 a0 a1 a2 a3 a4 a5 a6 a7 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 t3 t4 t5 t6"

for reg in $regs; do
  save="$(grep -oE "sd +$reg, +[0-9]+\(sp\)" "$sf" | sed -E 's/.*sd +[^,]+ +([0-9]+)\(sp\)/\1/' | tr -d ' ')"
  restore="$(grep -oE "ld +$reg, +[0-9]+\(sp\)" "$sf" | sed -E 's/.*ld +[^,]+ +([0-9]+)\(sp\)/\1/' | tr -d ' ')"
  [ -n "$save" ] || { echo "hidden: missing save for $reg" >&2; exit 1; }
  [ -n "$restore" ] || { echo "hidden: missing restore for $reg" >&2; exit 1; }
  [ "$save" = "$restore" ] || { echo "hidden: asymmetric slot for $reg (save=$save restore=$restore)" >&2; exit 1; }
  # each register must be saved/restored exactly once (single symmetric slot).
  [ "$(printf '%s\n' "$save" | wc -l | tr -d ' ')" = "1" ]
  [ "$(printf '%s\n' "$restore" | wc -l | tr -d ' ')" = "1" ]
done

# the frame is 32 registers * 8 bytes = 256 bytes and ends with sret.
grep -q 'addi sp, sp, -256' "$sf"
grep -q 'addi sp, sp, 256' "$sf"
grep -q 'sret' "$sf"

echo "hidden: kernelvec save/restore slots are symmetric and bounded"