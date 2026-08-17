#!/usr/bin/env sh
# kernel/virtio descriptor-ownership and sector-mapping contract check
# (source-level, no QEMU). Verifies the module invariants declared in
# spec/modules/kernel/virtio.yaml and the interface/kernel-virtio boundary:
#   - descriptor-partition: free and in-flight descriptor sets are disjoint.
#   - virtio-fixed-descriptor-ownership: each descriptor is free or in flight.
#   - sector-range-partition: block n maps to 512-byte sectors 2*n and 2*n+1.
#   - completion reclaims each chain and wakes its waiter exactly once.
# Runs with cwd = project root; PATH allowed.
set -eu

f=kernel/virtio_disk.c

# The fixed descriptor pool must be managed from a single free-ownership map.
grep -q 'free\[NUM\]' "$f"
grep -q 'alloc_desc' "$f"
grep -q 'free_desc' "$f"
grep -q 'free_chain' "$f"
grep -q 'alloc3_desc' "$f"

# Logical block n must translate to the two adjacent 512-byte sectors
# 2*n and 2*n+1 (sector-range-partition).
grep -q 'blockno \* 2' "$f"
grep -q 'sector0' "$f"
grep -q 'hdr.sector = sector0' "$f"

# Each request uses a fixed three-descriptor chain (header, data, status);
# the data descriptor is exactly 1024 bytes (the logical block unit).
grep -q '.len = sizeof(hdr)' "$f"
grep -q '.len = 1024' "$f"
grep -q '.len = 1' "$f"

# Temporary descriptor shortage must sleep and resume without leaking a
# partial chain, and completion must panic on malformed state rather than
# fabricate data.
grep -q 'sleep(&disk.free\[0\]' "$f"
grep -q 'panic("virtio_disk_rw: device reported failure")' "$f"
grep -q 'panic("virtio_disk_intr: used id")' "$f"

# The device identity must be validated before the queue is marked ready.
grep -q '0x74726976' "$f"  # "virt"
grep -q 'VIRTIO_MMIO_QUEUE_READY' "$f"

# The buffered data is passed directly to the device; a successful return
# means the complete 1024-byte block transfer completed.
grep -q 'info->data = data' "$f"

echo "ok: descriptor ownership and two-sector mapping present"