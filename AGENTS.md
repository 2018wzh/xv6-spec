# xv6 course workspace

Students handwrite Specs, run `vos spec lint` and `vos agent review`, revise the files, and commit them with ordinary Git before calling `vos agent implement`.

Module operations that cross an InterfaceSpec boundary should either match its operation names or state an explicit aggregate mapping. Descriptor-slot storage belongs to `kernel/process`; `kernel/file` owns the references stored in those slots. Raw block, inode, log, and disk exhaustion stay with their storage modules even when file syscalls expose the resulting error.

Generated checks use module-prefixed stable IDs and bind public, contract, fixed-seed fuzz, and bounded trace evidence to the ModuleSpec through `verifies`. The Agent returns those targets through the structured submission tool; it does not edit `vos.yaml` directly.

The linked worktree is a Git rollback boundary, not a process, network, credential, or host-filesystem sandbox. Host commands inherit the current user and network permissions.
