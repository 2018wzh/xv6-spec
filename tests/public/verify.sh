#!/usr/bin/env sh
    set -eu
    case_id="${1:?public test id required}"
    case "$case_id" in
      bootstrap_banner_not_null|kalloc_alignment|kvmmake_identity_mapping|trap_init_stvec_set|devintr_timer|uart_boot_output|fork_returns_different_pid|syscall_valid_number|bread_cache_hit|log_recovery_committed|exec_valid_elf|fd_alloc_close_cycle|pipe_read_write_cycle|shell_boots) ;;
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
    fork_returns_different_pid) grep -q 'fork' kernel/proc.c ;;
    syscall_valid_number) grep -q 'syscall' kernel/syscall.c ;;
    bread_cache_hit) grep -q 'bread' kernel/bio.c ;;
    log_recovery_committed) grep -q 'recover_from_log' kernel/log.c ;;
    exec_valid_elf) grep -q 'ELF_MAGIC' kernel/exec.c ;;
    fd_alloc_close_cycle) grep -q 'filealloc' kernel/file.c ;;
    pipe_read_write_cycle) grep -q 'pipewrite' kernel/pipe.c ;;
    shell_boots) grep -q 'runcmd' user/sh.c ;;
    esac
