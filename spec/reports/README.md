# Reports

This directory contains stage-specific and final project reports
that follow the contract defined in `spec/verification/report-contract.yaml`.

## Expected reports

- `stage-boot-report.md`
- `stage-memory-report.md`
- `stage-trap-report.md`
- `stage-process-report.md`
- `stage-syscall-report.md`
- `final-synthesis-report.md`

Each report must reference the relevant ArchitectureSlice, ModuleSpec,
OperationContract, verification evidence, and any SpecPatch applied.

## Generation flow

```bash
vos verify public
vos report generate --stage memory
vos report generate --final
```

`report generate` is strict: it requires
`spec/verification/report-contract.yaml`, public verification summaries,
commit ledger entries, and a valid `reporter.v2` Agent narrative. Stage
reports are written to `spec/reports/stage-<stage>-report.md`; machine-readable
summaries are written under `.vos/report/`. Successful generation creates a VOS
commit and appends a ledger entry for the new `HEAD`.
