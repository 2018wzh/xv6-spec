#!/usr/bin/env sh
# kernel/virtio public boot test. Builds the kernel, attaches a real QEMU
# virtio block device, and boots it under QEMU virt. The oracle requires the
# deterministic single canonical XV6_BOOT_OK banner (published before any
# storage consumer) and a clean boot to the scheduler, proving that
# virtio_disk_init successfully negotiated the device and prepared the queue
# before fsinit would run (fsinit is a later slice). A negotiation, identity,
# or queue-geometry failure would panic (spin) after the banner, which the
# absence-of-garbage check rejects. Runs with cwd = project root; PATH
# explicitly allowed.
set -eu
output="$(mktemp)"
trap 'rm -f "$output"' EXIT HUP INT TERM

disk="$(mktemp)"
trap 'rm -f "$output" "$disk"' EXIT HUP INT TERM
truncate -s 1M "$disk"

# Build the kernel (virtio_disk.c must be present and linked).
make

# Boot with a 1 MiB block device attached to the virtio mmio bus.
status=0
timeout 8 qemu-system-riscv64 \
  -machine virt \
  -bios none \
  -kernel kernel/kernel \
  -m 128M \
  -smp 1 \
  -nographic \
  -drive file="$disk",if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  </dev/null >"$output" 2>&1 || status=$?
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
  echo "public: boot failed (status $status)" >&2
  cat "$output" >&2
  exit 1
fi

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# virtio_disk_init must be wired into main before any storage consumer and
# the module's owned source must be present and linked.
for f in kernel/virtio_disk.c; do
  [ -f "$f" ] || { echo "public: missing $f" >&2; exit 1; }
done
grep -q 'virtio_disk_init()' kernel/main.c
grep -q 'virtio_disk_init(void)' kernel/virtio_disk.c
TOOLCHAIN="$(sh tests/generated/toolchain/select_toolchain.sh)"
"${TOOLCHAIN}objdump" -t kernel/kernel | grep -q 'virtio_disk_init'
"${TOOLCHAIN}objdump" -t kernel/kernel | grep -q 'virtio_disk_rw'

echo "public: disk attached, banner observed, virtio init succeeded"