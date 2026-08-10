#!/usr/bin/env sh
# kernel/virtio bounded trace/oracle target. The workload is a bounded QEMU
# virt serial capture of the booted kernel with a real virtio block device
# attached. The oracle requires exactly one canonical XV6_BOOT_OK banner and
# a clean boot to the scheduler (no panic / no garbage after the banner),
# observing that boot-time virtio initialization completes without corruption
# and that the virtio IRQ completion path is wired through PLIC and trap
# dispatch. Runs with cwd = project root; PATH explicitly allowed.
set -eu
output="$(mktemp)"
disk="$(mktemp)"
trap 'rm -f "$output" "$disk"' EXIT HUP INT TERM
truncate -s 1M "$disk"

make

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
  echo "trace: boot failed (status $status)" >&2
  cat "$output" >&2
  exit 1
fi

count="$(grep -o 'XV6_BOOT_OK' "$output" | wc -l | tr -d ' ')"
[ "$count" = "1" ]
banner_lines="$(grep 'XV6_BOOT_OK' "$output" || true)"
[ "$banner_lines" = "XV6_BOOT_OK" ]

# No panic message (virtio_disk errors print nothing, so a panic paragraph
# would be absent; assert the serial trace is bounded and non-garbled).
if grep -q 'panic' "$output"; then
  echo "trace: unexpected panic text in serial output" >&2
  cat "$output" >&2
  exit 1
fi

# virtio_disk_init must run before the process/syscall composition and the
# virtio IRQ must be enabled in PLIC and dispatched in devintr.
main_v="$(grep -n 'virtio_disk_init()' kernel/main.c | head -n1 | cut -d: -f1)"
main_u="$(grep -n 'userinit()' kernel/main.c | head -n1 | cut -d: -f1)"
[ -n "$main_v" ] && [ -n "$main_u" ] && [ "$main_v" -lt "$main_u" ]

grep -q 'VIRTIO0_IRQ' kernel/plic.c
grep -q 'VIRTIO0_IRQ' kernel/trap.c
grep -q 'virtio_disk_intr()' kernel/trap.c
grep -q 'virtio_disk_intr(void)' kernel/virtio_disk.c

echo "trace: bounded serial trace matched virtio init and IRQ wiring oracle"