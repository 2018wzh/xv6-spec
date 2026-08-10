# Makefile - Lab 2 build projection for the RISC-V kernel bootstrap.
# The capable RISC-V tool prefix is recomputed from the current PATH.

K = kernel

OBJS = \
  $(K)/entry.o \
  $(K)/start.o \
  $(K)/boot.o \
  $(K)/main.o \
  $(K)/string.o \
  $(K)/spinlock.o \
  $(K)/kalloc.o \
  $(K)/vm.o \
  $(K)/kernelvec.o \
  $(K)/trap.o \
  $(K)/plic.o \
  $(K)/uart.o \
  $(K)/console.o \
  $(K)/printk.o \
  $(K)/proc.o \
  $(K)/swtch.o

# select_riscv_toolchain: probe candidate prefixes and pick the first one
# that can compile an empty freestanding RV64 object. Empty on failure.
TOOLCHAIN := $(shell sh tests/generated/toolchain/select_toolchain.sh)
ifeq ($(strip $(TOOLCHAIN)),)
$(error no capable RISC-V toolchain found on PATH; run toolchain_capability_probe)
endif

CC = $(TOOLCHAIN)gcc
AS = $(TOOLCHAIN)gcc
LD = $(TOOLCHAIN)ld
OBJDUMP = $(TOOLCHAIN)objdump

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb \
         -gdwarf-2 -MD -mcmodel=medany -ffreestanding -fno-common \
         -nostdlib -fno-pic -mno-relax -fno-stack-protector -march=rv64gc -mabi=lp64
LDFLAGS = -z max-page-size=4096

kernel/kernel: $(OBJS) $(K)/kernel.ld
	$(LD) $(LDFLAGS) -T $(K)/kernel.ld -o $@ $(OBJS)
	@echo "+ $@"

$(K)/%.o: $(K)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(K)/%.o: $(K)/%.S
	$(CC) $(CFLAGS) -c $< -o $@

# toolchain-probe: compile an empty freestanding RV64 object with the
# selected prefix to prove compilation capability (not just command presence).
.PHONY: toolchain-probe
toolchain-probe:
	sh tests/generated/toolchain/probe_test.sh

.PHONY: clean
clean:
	rm -f $(K)/*.o $(K)/*.d kernel/kernel

.PHONY: qemu
qemu: kernel/kernel
	qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic </dev/null