#!/usr/bin/env bash
# Lab 7 CTF artifact portability contract: QEMU consumes the freestanding ELF
# as non-empty input data; host executable permission is not its acceptance
# criterion, while the existing serial oracle remains authoritative.
set -eu

script=tests/generated/lab/ctf-warmup/run-tests.sh
elf=lab1/build/ctf-baremetal.elf

grep -Fq 'test -s "$ROOT/lab1/build/ctf-baremetal.elf"' "$script"
if grep -Fq 'test -x "$ROOT/lab1/build/ctf-baremetal.elf"' "$script"; then
  echo "contract: bare-metal ELF still requires host executable permission" >&2
  exit 1
fi

bash "$script" baremetal 17
[ -s "$elf" ] || { echo "contract: QEMU input ELF is empty" >&2; exit 1; }

echo "ctf-warmup-artifact-portability-contract: non-empty QEMU input reached the serial oracle"
