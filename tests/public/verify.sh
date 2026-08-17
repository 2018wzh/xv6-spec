#!/usr/bin/env sh
    set -eu
    case_id="${1:?public test id required}"
    case "$case_id" in
      bootstrap_banner_not_null|kalloc_alignment|kvmmake_identity_mapping|trap_init_stvec_set|devintr_timer|uart_boot_output) ;;
      *) echo "unknown current-lab check: $case_id" >&2; exit 2 ;;
    esac
    test -f vos.yaml
    test -d spec
    case "$case_id" in
        bootstrap_banner_not_null) grep -q 'boot_banner' kernel/boot.c ;;
    kalloc_alignment) grep -q 'PGSIZE' kernel/kalloc.c ;;
    kvmmake_identity_mapping) grep -q 'kvmmake' kernel/vm.c ;;
    trap_init_stvec_set) grep -q 'w_stvec' kernel/trap.c ;;
    devintr_timer) grep -q 'timer' kernel/trap.c ;;
    uart_boot_output) grep -q 'uart' kernel/uart.c ;;
    esac
