#!/usr/bin/env sh
# kernel/virtio fixed-seed descriptor-ownership fuzz driver. Compiles the
# deterministic host-model harness with `cc` and runs it with the given seed,
# case count, and reproduction artifact path written only on failure. The
# harness exercises descriptor allocation, wraparound, and reclamation of the
# fixed pool (descriptor-partition / virtio-fixed-descriptor-ownership).
# Runs with cwd = project root; PATH is explicitly allowed.
set -eu
seed="${1:?seed required}"
cases="${2:?cases required}"
repro="${3:?reproduction artifact path required}"

dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -O2 -Wall -o "$tmp/fuzz_ring" "$dir/fuzz_ring.c"

"$tmp/fuzz_ring" "$seed" "$cases" "$repro"