#!/usr/bin/env sh
    set -eu
    case_id="${1:?public test id required}"
    case "$case_id" in
      bootstrap_banner_not_null) ;;
      *) echo "unknown current-lab check: $case_id" >&2; exit 2 ;;
    esac
    test -f vos.yaml
    test -d spec
    case "$case_id" in
        bootstrap_banner_not_null) grep -q 'boot_banner' kernel/boot.c ;;
    esac
