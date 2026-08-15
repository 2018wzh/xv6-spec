# VisionFive 2 hardware port

This module owns the fail-closed VisionFive 2 port for the StarFive
VisionFive 2 (four U74 application harts on a JH7110) and its evidence
boundary. The QEMU course kernel under `kernel/` is intentionally left
untouched; the physical-board kernel is the separate vendor tree under
`platform/visionfive2/kernel/`, built with `PLATFORM=visionfive2`.

## What is supplied by the operator

`validate_board_inputs` (spec/modules/platform-visionfive2.yaml) requires every
physical input to be explicit before any hardware workload can be reviewed:

1. **board identity** — the specific VisionFive 2 unit and its four U74 harts.
2. **firmware** — the OpenSBI + U-Boot image that will be flashed.
3. **DTB** — the pinned board device tree from `hardware/visionfive2/sources.lock`.
4. **image** — the FIT/OS image loaded by the boot chain.
5. **serial device** — the physical UART endpoint and its baud rate.
6. **workload** — the exact four-hart usertest workload to run.

The runner is fail-closed by construction: nobody claims a passed hardware result without physical execution and human review. The runner records SHA-256 hashes of the FIT, kernel image, DTB, and xv6
filesystem image before it opens the serial endpoint. Missing inputs, a dirty
Git HEAD, an unknown board alias, or a hash mismatch fail closed and produce
no passed hardware evidence.

## Evidence rules (fail-closed by construction)

- Local QEMU regression, fixture serial, and image inspection **cannot** change
  hardware evidence from `pending_human_review` to `passed`
  (`hardware-evidence-boundary`).
- Four-hart usertests require physical execution and a separate human review
  (`vf2-human-review-required`, interface/visionfive2-board.yaml).
- Automatic evidence remains `pending_human_review`; acceptance is left to
  human review.
- Bounded serial timeout terminates the run; one hardware runner owns one
  serial endpoint.
- Board interrupt behavior (SBI TIME, IPI, HSM, RFENCE, SRST) is **observed**,
  never inferred from QEMU.
- Missing inputs or probes never become a successful board claim.

## Boot and storage contract

U-Boot reads the FIT `xv6.itb` from the FAT partition selected by
`VOS_VF2_BOOT_PART` and boots it with `bootm`. The S-mode kernel is linked at
`0x40200000`, receives the hart ID in `a0` and the hash-verified DTB in `a1`,
parses RAM/reserved-memory/UART/PLIC/SDIO from the DTB, probes the required SBI
extensions, and starts the remaining three U74 harts through SBI HSM.

The kernel discovers the xv6 filesystem by walking the on-disk GPT: the
partition must carry the Linux filesystem-data type GUID
`0FC63DAF-8483-4772-8E79-3D69D8477DE4` and the UTF-16 name `xv6fs`.
The FAT partition may be any GPT partition number the board firmware exposes.

## Running the generated checks

```sh
# candidate boundary (canonical, spec-level)
sh tests/generated/platform/visionfive2/candidate_contract.sh
# public: required board inputs + fail-closed runner
sh tests/generated/platform/visionfive2/public_board_inputs.sh
sh tests/generated/platform/visionfive2/public_failclosed_runner.sh
# contract: validate_board_inputs + evidence boundary
sh tests/generated/platform/visionfive2/contract_validate_inputs.sh
sh tests/generated/platform/visionfive2/contract_evidence_boundary.sh
# fixed-seed fuzz: input-completeness and hash-record decision model
sh tests/generated/platform/visionfive2/fuzz_input_state.sh 42 500
sh tests/generated/platform/visionfive2/fuzz_hash_scheme.sh 42 500
# bounded trace/oracle: serial workload bounding + evidence reproducibility
sh tests/generated/platform/visionfive2/trace_serial_workload.sh
```
