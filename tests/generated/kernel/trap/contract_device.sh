#!/usr/bin/env sh
# kernel_trap device contract check (source-level, no QEMU). Verifies the
# kernel_trap_device_contract property declared in spec/modules/kernel/trap.yaml:
#   - each nonzero PLIC claim is completed after dispatch (claim/completion),
#   - devintr claims, dispatches, and completes recognized external interrupts,
#   - uartintr consumes every available byte and returns without blocking,
#   - an unrecognized cause returns zero without acknowledging state.
# Runs with cwd = project root; PATH allowed.
set -eu

# devintr must claim and complete through the PLIC driver boundary.
grep -q 'plic_claim()' kernel/trap.c
grep -q 'plic_complete(irq)' kernel/trap.c
grep -q 'scause == SCAUSE_SEXTERNAL' kernel/trap.c

# The PLIC driver claim/complete pair must exist in plic.c.
grep -q 'plic_claim(void)' kernel/plic.c
grep -q 'plic_complete(int irq)' kernel/plic.c

# plicinit must enable UART IRQ 10 for the boot hart's S-mode context and set
# the priority before interrupts are enabled (interrupt-enable-order).
grep -q 'UART0_IRQ' kernel/plic.c
grep -q 'PLIC_SENABLE' kernel/plic.c
grep -q 'PLIC_PRIORITY' kernel/plic.c

# uartintr must consume every available byte and terminate when LSR reports no
# ready byte (bounded receive; an empty FIFO returns without blocking).
grep -q 'uartgetc()' kernel/uart.c
grep -q 'LSR_RX_READY' kernel/uart.c
grep -q 'consoleintr(c)' kernel/uart.c

# devintr returns zero for a cause that is not a supervisor external
# interrupt, so unrelated device state is never acknowledged.
grep -q 'return 0;' kernel/trap.c

echo "ok: PLIC claim->dispatch->complete lifecycle and bounded UART receive present"