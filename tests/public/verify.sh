#!/usr/bin/env sh
    set -eu
    case_id="${1:?public test id required}"
    case "$case_id" in
      bootstrap_banner_not_null|bootstrap_qemu_single_banner|toolchain_capability_probe|toolchain_clean_rebuild) ;;
      *) echo "unknown current-lab check: $case_id" >&2; exit 2 ;;
    esac
    test -f vos.yaml
    test -d spec
    case "$case_id" in
        bootstrap_banner_not_null) grep -q 'boot_banner' kernel/boot.c ;;
        bootstrap_qemu_single_banner)
          output="$(mktemp)"
          trap 'rm -f "$output"' EXIT HUP INT TERM
          . tests/public/lab2-boot.sh
          vos_lab2_capture_serial "$output"
          vos_lab2_require_single_banner "$output"
          ;;
        toolchain_capability_probe)
          sh tests/generated/toolchain/probe_test.sh
          ;;
        toolchain_clean_rebuild)
          sh tests/generated/toolchain/rebuild_test.sh
          ;;
    esac
