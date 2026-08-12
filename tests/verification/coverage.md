# Lab 10 verification coverage

This table records the deterministic acceptance boundary at the Lab 10
candidate commit. The authoritative target definitions and `verifies` bindings
remain in `vos.yaml`; this document explains why the main evidence families are
not interchangeable.

| Spec ID | Property family | Evidence targets | Candidate result |
| --- | --- | --- | --- |
| `lab/ctf-warmup` | Linux and bare-metal CTF behavior | `ctf-warmup-*` | pass |
| `kernel/boot` | entry, PMP, banner, linker projection | `kernel_boot_*` | pass |
| `kernel/memory` | allocation, mapping and copy ownership | `kernel_memory_*` | pass |
| `kernel/trap` | vector, frame and device dispatch | `kernel_trap_*` | pass |
| `kernel/process` | scheduling, rollback and process tree | `kernel_process_*` | pass |
| `kernel/syscall` | bounded dispatch and user-pointer validation | `kernel_syscall_*` | pass |
| `kernel/virtio`, `kernel/bio`, `kernel/log`, `kernel/inode`, `kernel/file` | persistent resource lifecycle | corresponding public/contract/fuzz/trace families | pass |
| `kernel/pipe` | FIFO, close and endpoint lifetime | `kernel_pipe_*` | pass |
| `process-tree-reliability` | seed 42, 5000 lifecycle cases | `kernel_process_tree_fuzz` | pass |
| `platform/visionfive2`, `interface/visionfive2-board` | candidate boundary only | `platform_visionfive2_candidate_contract` | pass; physical run unavailable |

The deterministic verification run is `202608121251049-9e35f531`. It passed
build and every public, contract, fixed-seed fuzz, and bounded trace target.
Hardware run `202608121246535-7cde3eae` failed closed and remained
`pending_human_review`; it is evidence of an unresolved gate, not a pass.
