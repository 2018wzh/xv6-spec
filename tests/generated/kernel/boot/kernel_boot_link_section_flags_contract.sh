#!/usr/bin/env sh
# Lab 7 linker portability contract: the executable trampoline section must
# retain the canonical entry point and fit in its single page.
set -eu

prefix="$(sh tests/generated/toolchain/select_toolchain.sh)"
objdump="${prefix}objdump"

make >/dev/null

entry="$($objdump -t kernel/kernel | awk '$NF == "_entry" { print $1; exit }')"
trampoline="$($objdump -t kernel/kernel | awk '$NF == "trampoline" { print $1; exit }')"
uservec="$($objdump -t kernel/kernel | awk '$NF == "uservec" { print $1; exit }')"
userret="$($objdump -t kernel/kernel | awk '$NF == "userret" { print $1; exit }')"

[ "$entry" = "0000000080000000" ] || {
  echo "contract: unexpected _entry address $entry" >&2
  exit 1
}
[ -n "$trampoline" ] && [ -n "$uservec" ] && [ -n "$userret" ] || {
  echo "contract: trampoline symbols are incomplete" >&2
  exit 1
}
case "$trampoline" in
  *000) ;;
  *) echo "contract: trampoline is not page aligned: $trampoline" >&2; exit 1 ;;
esac

section="$($objdump -h kernel/kernel | awk '
  $2 == ".trampoline" { print; getline; print; exit }
')"
printf '%s\n' "$section" | grep -Eq 'CODE|TEXT' || {
  echo "contract: .trampoline is not executable" >&2
  exit 1
}
size_hex="$(printf '%s\n' "$section" | awk 'NR == 1 { print $3 }')"
[ $((0x$size_hex)) -le 4096 ] || {
  echo "contract: .trampoline exceeds one page: 0x$size_hex" >&2
  exit 1
}

echo "kernel_boot_link_section_flags_contract: executable trampoline layout preserved"
