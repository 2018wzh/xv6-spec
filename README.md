# xv6-spec Lab 5

This slice adds the process table, first user process, context switching, user trap-frame ABI, and validated minimal syscall path. `kernel/process` owns lifecycle and scheduling; `kernel/syscall` owns dispatch and the trampoline. Filesystem and persistent user programs remain later-lab work.
