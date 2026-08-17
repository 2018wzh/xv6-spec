# Lab 10 failure analysis

## Scheduler lifecycle oracle

**Symptom.** Deterministic verification run `202608121158205-35a8ff1d`
reported only `kernel_process_lifecycle_contract` as failed.

**Evidence and eliminated hypotheses.** The RISC-V kernel built, QEMU booted,
and the remaining cumulative checks passed. Running the contract directly
showed that it expected `sched()` to require `RUNNING`, although a process must
publish `RUNNABLE`, `SLEEPING`, or `ZOMBIE` before switching to the scheduler.
This excluded a compiler, QEMU, or process implementation failure.

**Root cause.** The older Lab 5 text oracle encoded the inverse of the scheduler
state invariant.

**Repair and regression.** The student changed the contract to require the
fail-fast `sched: still RUNNING` guard. The focused contract then passed, and
the later full verification run `202608121251049-9e35f531` passed every target.
The original failed run remains in `.vos` and is referenced by the repair
commit; it was not overwritten or reclassified.
