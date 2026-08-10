# Makefile - cumulative Lab 1 CTF and Lab 2 kernel build projection.

K := kernel
LAB1_BUILD := lab1/build
BUN ?= bun
HOST_CC ?= gcc
HOST_CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
SEED ?= 0x5eed0001

LAB1_LINUX_SRCS := lab1/linux/flag-reader.c lab1/linux/sha256.c
LAB1_BAREMETAL_SRCS := lab1/baremetal/start.S lab1/baremetal/uart.c lab1/baremetal/sha256.c lab1/baremetal/support.c lab1/baremetal/main.c

# Probe candidate prefixes and select the first compiler that can build a
# freestanding RV64 object. Selection is recomputed from the current PATH.
TOOLCHAIN := $(shell sh tests/generated/toolchain/select_toolchain.sh)
ifeq ($(strip $(TOOLCHAIN)),)
$(error no capable RISC-V toolchain found on PATH; run toolchain_capability_probe)
endif

RISCV_CC := $(TOOLCHAIN)gcc
RISCV_LD := $(TOOLCHAIN)ld
RISCV_OBJCOPY := $(TOOLCHAIN)objcopy
KERNEL_CFLAGS := -Wall -Werror -O -fno-omit-frame-pointer -ggdb \
  -gdwarf-2 -MD -mcmodel=medany -ffreestanding -fno-common \
  -nostdlib -fno-pic -mno-relax -fno-stack-protector -march=rv64gc -mabi=lp64
RISCV_LDFLAGS := -z max-page-size=4096
CTF_RISCV_CFLAGS := -O2 -Wall -Wextra -march=rv64gc -mabi=lp64 -mcmodel=medany \
  -static -nostdlib -nostartfiles -ffreestanding

KERNEL_OBJS := \
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

.PHONY: all clean qemu ctf-qemu gen-fixture toolchain-probe

all: kernel/kernel

kernel/kernel: $(KERNEL_OBJS) $(K)/kernel.ld
	$(RISCV_LD) $(RISCV_LDFLAGS) -T $(K)/kernel.ld -o $@ $(KERNEL_OBJS)
	@echo "+ $@"

$(K)/%.o: $(K)/%.c
	$(RISCV_CC) $(KERNEL_CFLAGS) -c $< -o $@

$(K)/%.o: $(K)/%.S
	$(RISCV_CC) $(KERNEL_CFLAGS) -c $< -o $@

$(LAB1_BUILD):
	mkdir -p $(LAB1_BUILD)

$(LAB1_BUILD)/.dir-stamp: | $(LAB1_BUILD)
	@touch $@

gen-fixture: | $(LAB1_BUILD)/.dir-stamp
	$(BUN) tests/public/ctf-fixture.ts generate $(LAB1_BUILD)/fixture $(SEED)
	test -f $(LAB1_BUILD)/fixture/flags.img
	test -f $(LAB1_BUILD)/fixture/metadata.json

$(LAB1_BUILD)/flag-reader: $(LAB1_LINUX_SRCS) lab1/linux/sha256.h | $(LAB1_BUILD)/.dir-stamp
	$(HOST_CC) $(HOST_CFLAGS) -I lab1/linux -o $@ $(LAB1_LINUX_SRCS)

$(LAB1_BUILD)/flags.img: gen-fixture
	cp $(LAB1_BUILD)/fixture/flags.img $@

$(LAB1_BUILD)/flags_img.o: $(LAB1_BUILD)/flags.img
	cd $(LAB1_BUILD) && $(RISCV_OBJCOPY) -I binary -O elf64-littleriscv -B riscv flags.img flags_img.o

$(LAB1_BUILD)/ctf-baremetal.elf: $(LAB1_BAREMETAL_SRCS) lab1/baremetal/sha256.h lab1/baremetal/uart.h lab1/baremetal/linker.ld $(LAB1_BUILD)/flags_img.o | $(LAB1_BUILD)/.dir-stamp
	$(RISCV_CC) $(CTF_RISCV_CFLAGS) -I lab1/baremetal -T lab1/baremetal/linker.ld \
		$(LAB1_BAREMETAL_SRCS) $(LAB1_BUILD)/flags_img.o -o $@

toolchain-probe:
	sh tests/generated/toolchain/probe_test.sh

qemu: kernel/kernel
	qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic </dev/null

ctf-qemu: $(LAB1_BUILD)/ctf-baremetal.elf
	@set -o pipefail; \
	qemu-system-riscv64 -machine virt -m 128M -nographic -no-reboot -bios none \
		-kernel $(LAB1_BUILD)/ctf-baremetal.elf 2>&1 | tee $(LAB1_BUILD)/baremetal.log; \
	grep -q 'CTF_BAREMETAL_OK' $(LAB1_BUILD)/baremetal.log

clean:
	rm -f $(K)/*.o $(K)/*.d kernel/kernel
	rm -rf $(LAB1_BUILD)
