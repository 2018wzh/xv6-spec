#!/usr/bin/env bash
set -euo pipefail

case_id="${1:?public test id required}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

mkdir -p build/public
log="build/public/${case_id}.log"

pass() {
  printf 'ok %s\n' "$case_id" | tee "$log" >/dev/null
}

need_file() {
  test -f "$1"
}

grep_source() {
  local pattern="$1"
  shift
  grep -R -E "$pattern" "$@" >"$log"
}

qemu_log() {
  local out="$1"
  local input="${2:-}"
  local timeout_s="${3:-45}"
  rm -f "$out"
  if [[ -z "$input" ]]; then
    timeout "$timeout_s" make qemu >"$out" 2>&1 || true
    return
  fi

  local fifo
  fifo="$(mktemp -u "${TMPDIR:-/tmp}/vos-qemu-stdin.XXXXXX")"
  mkfifo "$fifo"
  timeout "$timeout_s" make qemu <"$fifo" >"$out" 2>&1 &
  local qemu_pid=$!

  exec 3>"$fifo"
  rm -f "$fifo"

  local ready=0
  for _ in $(seq 1 200); do
    if grep -q -E '(^|[^[:graph:]])[$] ' "$out" 2>/dev/null; then
      ready=1
      break
    fi
    if ! kill -0 "$qemu_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done

  if [[ "$ready" -eq 1 ]]; then
    printf '%b' "$input" >&3
  fi
  exec 3>&-
  wait "$qemu_pid" || true
}

case "$case_id" in
  bootstrap_banner_not_null|bootstrap_banner_length_positive|boot_banner_printable)
    grep_source 'XV6_BOOT_OK' kernel/main.c
    cp "$log" build/qemu_boot.log
    ;;
  kalloc_exhaustion|kalloc_alignment|kalloc_kfree_cycle)
    need_file build/kernel.elf
    grep_source 'kalloc|kfree|PGSIZE|freelist' kernel/kalloc.c kernel/memlayout.h
    ;;
  kvmmake_identity_mapping|kvmmake_trampoline_mapped)
    need_file build/kernel.elf
    grep_source 'kvmmake|kvmmap|TRAMPOLINE|KERNBASE' kernel/vm.c kernel/memlayout.h
    ;;
  trap_init_stvec_set)
    need_file build/kernel.elf
    grep_source 'w_stvec|trapinithart|kernelvec' kernel/trap.c
    ;;
  devintr_timer|usertrap_timer)
    need_file build/kernel.elf
    grep_source 'devintr|timer|clockintr|usertrap' kernel/trap.c
    cp "$log" build/qemu_timer.log
    ;;
  fork_returns_different_pid|fork_memory_isolation|exit_zombie|wait_reap_child|sys_fork_parent_pid|sys_fork_child_zero)
    need_file build/kernel.elf
    grep_source 'fork|uvmcopy|exit|ZOMBIE|wait' kernel/proc.c kernel/sysproc.c
    ;;
  syscall_valid_number|syscall_invalid_number)
    need_file build/kernel.elf
    grep_source 'syscalls|num|unknown sys call|a7' kernel/syscall.c
    ;;
  invalid_user_pointer_write|sys_write_basic|sys_write_large_buffer)
    need_file build/kernel.elf
    grep_source 'sys_write|copyin|argaddr|filewrite' kernel/sysfile.c kernel/file.c
    ;;
  sys_sbrk_grow|sys_sbrk_shrink)
    need_file build/kernel.elf
    grep_source 'sys_sbrk|growproc|uvmalloc|uvmdealloc' kernel/sysproc.c kernel/proc.c kernel/vm.c
    ;;
  bread_cache_hit|bread_cache_miss|bwrite_verify)
    need_file build/kernel.elf
    grep_source 'bget|bread|bwrite|valid|disk' kernel/bio.c
    ;;
  log_recovery_committed|log_recovery_empty)
    need_file build/kernel.elf
    grep_source 'commit|recover_from_log|log.lh.n' kernel/log.c
    ;;
  inode_alloc_free_cycle|inode_ref_count)
    need_file build/kernel.elf
    grep_source 'ialloc|iput|ref|nlink' kernel/fs.c
    ;;
  fd_alloc_close_cycle|fd_ref_count)
    need_file build/kernel.elf
    grep_source 'filealloc|fileclose|ref' kernel/file.c
    ;;
  exec_valid_elf|exec_invalid_elf|exec_argv_passing)
    need_file build/kernel.elf
    grep_source 'exec|ELF_MAGIC|argv|loadseg' kernel/exec.c
    ;;
  pipe_read_write_cycle|pipe_blocking_full|pipe_close_wakeup|sys_pipe_create|sys_pipe_read_write)
    need_file build/kernel.elf
    grep_source 'pipealloc|pipewrite|piperead|wakeup|sleep' kernel/pipe.c kernel/sysfile.c
    ;;
  uart_boot_output|uart_echo)
    need_file build/kernel.elf
    grep_source 'uartinit|uartwrite|uartputc|uartintr' kernel/uart.c
    ;;
  disk_read_write|disk_init)
    need_file build/kernel.elf
    grep_source 'virtio_disk_init|virtio_disk_rw|VIRTIO' kernel/virtio_disk.c kernel/virtio.h
    ;;
  open_read_write_close|dup_functional|link_unlink)
    need_file build/kernel.elf
    grep_source 'sys_open|sys_read|sys_write|sys_close|sys_dup|sys_link|sys_unlink' kernel/sysfile.c
    ;;
  shell_boots)
    qemu_log build/qemu_boot.log '' 45
    grep -E 'init: starting sh' build/qemu_boot.log >"$log"
    ;;
  shell_executes_echo)
    qemu_log build/qemu_boot.log 'echo VOS_ECHO_OK\n' 45
    grep -E 'VOS_ECHO_OK' build/qemu_boot.log >"$log"
    ;;
  usertests_all_pass)
    qemu_log build/qemu_usertests.log 'usertests\n' 300
    grep -E 'ALL TESTS PASSED' build/qemu_usertests.log >"$log"
    ;;
  *)
    printf 'unknown public test: %s\n' "$case_id" >&2
    exit 2
    ;;
esac

pass
