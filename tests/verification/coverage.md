# Lab 10 verification coverage

This table records the deterministic acceptance boundary at the Lab 10
completion commit. The authoritative target definitions and `verifies` bindings
remain in `vos.yaml`; this document explains why the main evidence families are
not interchangeable.

| Spec ID | Property family | Evidence targets | Result |
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
| `platform/visionfive2`, `interface/visionfive2-board` | fail-closed board contract plus four-hart physical workload | `platform_visionfive2_candidate_contract` and retained board evidence | contract pass; physical `usertests` reached `ALL TESTS PASSED` |

The deterministic verification run is `202608121251049-9e35f531`. It passed
build and every public, contract, fixed-seed fuzz, and bounded trace target.
The earlier hardware run `202608121246535-7cde3eae` failed closed and remains a
valid failure record. The later 2026-08-16 VisionFive 2 run retained under
`hardware/visionfive2/evidence/` completed the four-hart workload. Portal still
requires the serial log and hardware report to be uploaded and approved by a
teacher before the course submission becomes complete.
