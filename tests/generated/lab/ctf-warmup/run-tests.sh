#!/usr/bin/env bash
#
# Lab 1 CTF warm-up test harness. VOS invokes this for the generated public,
# contract, fixed-seed fuzz, and bounded trace/oracle test targets.
#
# Reader start order: for every seed we run the Linux reader first, then the
# bare-metal reader, and cross-check both against the fixture metadata oracle
# and against each other (source-equivalence).
#
# Usage: run-tests.sh <mode> [seed] [seed...]
#   mode = linux        run only the Linux reader and verify its records
#   mode = baremetal    run only the QEMU guest and verify its records
#   mode = contract     run both readers on one fixture and cross-check them
#   mode = fuzz         run both readers over the supplied list of seeds
#   mode = trace        run both readers and verify ordered records for the
#                       supplied seeds against the fixture metadata oracle
#   mode = hidden       run both readers and cross-check equivalence

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# tests/generated/lab/ctf-warmup -> repository root
ROOT="$(cd "$HERE/../../../.." && pwd)"

BUN="${BUN:-bun}"
MODE="${1:?missing mode}"
shift

BUILD="$ROOT/lab1/build"
FIX="$BUILD/test-fixture"
LINUX_LOG="$BUILD/verify-linux.log"
BM_LOG="$BUILD/verify-baremetal.log"
BM_SEED_FILE="$BUILD/.bm-seed"

ensuredirs() { mkdir -p "$BUILD"; }

build_linux() {
    if [[ ! -x "$ROOT/lab1/build/flag-reader" ]]; then
        (cd "$ROOT" && make lab1/build/flag-reader >/dev/null)
    fi
}

# The bare-metal ELF embeds a fixture image built at SEED, so it is rebuilt only
# when the active seed changes to keep multi-seed runs bounded.
build_baremetal_for_seed() {
    local seed="$1"
    ensuredirs
    local stored=""
    if [[ -f "$BM_SEED_FILE" ]]; then stored="$(cat "$BM_SEED_FILE")"; fi
    if [[ "$stored" != "$seed" ]]; then
        (cd "$ROOT" && make SEED="$seed" lab1/build/ctf-baremetal.elf >/dev/null)
        printf '%s' "$seed" >"$BM_SEED_FILE"
    fi
    test -x "$ROOT/lab1/build/ctf-baremetal.elf"
}

gen_fixture() {
    local seed="$1"
    rm -rf "$FIX"
    (cd "$ROOT" && "$BUN" tests/public/ctf-fixture.ts generate "$FIX" "$seed" >/dev/null)
    test -f "$FIX/metadata.json"
}

run_linux() {
    "$ROOT/lab1/build/flag-reader" "$FIX" >"$LINUX_LOG"
}

run_baremetal() {
    local out
    out="$(cd "$ROOT" && qemu-system-riscv64 -machine virt -m 128M -nographic -no-reboot \
        -bios none -kernel "$ROOT/lab1/build/ctf-baremetal.elf" 2>&1)"
    printf '%s\n' "$out" >"$BM_LOG"
    if ! grep -q 'CTF_BAREMETAL_OK' "$BM_LOG"; then
        echo "run-tests: missing CTF_BAREMETAL_OK marker" >&2
        exit 1
    fi
}

verify_linux() {
    (cd "$ROOT" && "$BUN" tests/public/ctf-fixture.ts verify "$FIX/metadata.json" "$LINUX_LOG")
}

verify_pair() {
    (cd "$ROOT" && "$BUN" tests/public/ctf-fixture.ts verify "$FIX/metadata.json" "$LINUX_LOG" "$BM_LOG")
    grep 'CTF_RECORD' "$LINUX_LOG" >"$BUILD/.l.txt"
    grep 'CTF_RECORD' "$BM_LOG" >"$BUILD/.b.txt"
    diff -u "$BUILD/.l.txt" "$BUILD/.b.txt" >/dev/null \
        || { echo "run-tests: reader records diverge" >&2; exit 1; }
}

run_seed_pair() {
    local seed="$1"
    build_linux
    build_baremetal_for_seed "$seed"
    gen_fixture "$seed"
    run_linux
    run_baremetal
    verify_pair
}

seeds=("${@:-0x5eed0001}")

case "$MODE" in
    linux)
        ensuredirs; build_linux
        gen_fixture "${seeds[0]}"
        run_linux
        verify_linux
        ;;
    baremetal)
        build_baremetal_for_seed "${seeds[0]}"
        gen_fixture "${seeds[0]}"
        run_baremetal
        (cd "$ROOT" && "$BUN" tests/public/ctf-fixture.ts verify "$FIX/metadata.json" "$BM_LOG")
        ;;
    contract)
        run_seed_pair "${seeds[0]}"
        ;;
    fuzz)
        for seed in "${seeds[@]}"; do run_seed_pair "$seed"; done
        ;;
    trace)
        for seed in "${seeds[@]}"; do run_seed_pair "$seed"; done
        ;;
    hidden)
        run_seed_pair "${seeds[0]}"
        ;;
    *)
        echo "run-tests: unknown mode '$MODE'" >&2
        exit 2
        ;;
esac

echo "run-tests($MODE): PASS"
