# VisionFive 2 candidate boundary

This slice defines the fail-closed port boundary for the StarFive VisionFive 2
(four U74 application harts on a JH7110). No board implementation exists here
yet, and the hardware runner is deliberately fail-closed: nobody claims a
passed hardware result unless a physical board, serial endpoint, and immutable
workload image were actually exercised and then reviewed by a human.

## What is supplied by the operator

`validate_board_inputs` (spec/modules/platform-visionfive2.yaml) requires every
physical input to be explicit before any hardware workload can be reviewed:

1. **board identity** — the specific VisionFive 2 unit and its four U74 harts.
2. **firmware** — the OpenSBI + U-Boot image that will be flashed.
3. **DTB** — the board device tree.
4. **image** — the FIT/OS image to be loaded by the boot chain.
5. **serial device** — the physical UART endpoint and its baud rate.
6. **workload** — the exact four-hart usertest workload to run.

The runner records the hashes of these immutable inputs before it launches the
declared workload. If any of them is missing, unknown, or mismatches, the run
fails closed and produces **no** passed hardware evidence.

## Evidence rules (fail-closed by construction)

- Local QEMU regression, fixture serial, and image inspection **cannot** change
  hardware evidence from `pending_human_review` to `passed`
  (`hardware-evidence-boundary`).
- Four-hart usertests require physical execution and a separate human review
  (`vf2-human-review-required`, interface/visionfive2-board.yaml).
- Automatic evidence remains `pending_human_review`; acceptance is left to
  human review (concurrency guarantee in spec/modules/platform-visionfive2.yaml).
- Bounded serial timeout terminates the run; one hardware runner owns one
  serial endpoint.
- Board interrupt behavior (SBI TIME, IPI, HSM, RFENCE, SRST) is **observed**,
  never inferred from QEMU.
- Missing inputs or probes never become a successful board claim.

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
cc -O2 -Wall -o /tmp/vf2_state \
  tests/generated/platform/visionfive2/input_state_fuzz.c
/tmp/vf2_state 42 500 tests/generated/platform/visionfive2/repro/platform_visionfive2_input_state_fuzz.repro
cc -O2 -Wall -o /tmp/vf2_hash \
  tests/generated/platform/visionfive2/hash_scheme_fuzz.c
/tmp/vf2_hash 42 500 tests/generated/platform/visionfive2/repro/platform_visionfive2_hash_scheme_fuzz.repro
# bounded trace/oracle: serial workload bounding + evidence reproducibility
sh tests/generated/platform/visionfive2/trace_serial_workload.sh
```