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
  -nostdlib -fno-pic -fno-stack-protector -march=rv64gc -mabi=lp64 \
  -Wno-gnu-designator
RISCV_LDFLAGS := -m elf64lriscv -z max-page-size=4096
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
  $(K)/trampoline.o \
  $(K)/trap.o \
  $(K)/plic.o \
  $(K)/uart.o \
  $(K)/console.o \
  $(K)/printk.o \
  $(K)/proc.o \
  $(K)/exec.o \
  $(K)/swtch.o \
  $(K)/syscall.o \
  $(K)/sysproc.o \
  $(K)/virtio_disk.o \
  $(K)/log.o \
  $(K)/bio.o \
  $(K)/fs.o \
  $(K)/file.o \
  $(K)/sysfile.o \
  $(K)/pipe.o

.PHONY: all clean qemu ctf-qemu gen-fixture toolchain-probe user-fstest

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

# ---- kernel/inode: deterministic root image (mkfs) ----
# mkfs is a host tool that deterministically writes the root fs.img consumed
# by the kernel at mount time. fs.img is a disposable build artifact.
mkfs/mkfs: mkfs/mkfs.c
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $<

fs.img: mkfs/mkfs
	./mkfs/mkfs fs.img

qemu: kernel/kernel fs.img
	qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic \
		-drive file=fs.img,if=none,format=raw,id=x0 \
		-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 </dev/null

ctf-qemu: $(LAB1_BUILD)/ctf-baremetal.elf
	@set -o pipefail; \
	qemu-system-riscv64 -machine virt -m 128M -nographic -no-reboot -bios none \
		-kernel $(LAB1_BUILD)/ctf-baremetal.elf 2>&1 | tee $(LAB1_BUILD)/baremetal.log; \
	grep -q 'CTF_BAREMETAL_OK' $(LAB1_BUILD)/baremetal.log

# ---- kernel/file: bounded user workload (optional; not part of `all`) ----
# The Lab 6 file ABI's bounded user workload (user/fstest.c) compiles as a
# freestanding RISC-V binary linked with kernel/user.ld. It is not loaded by
# the kernel yet (no exec/fork); building it validates the user ABI surface
# without affecting the kernel image. The binary is a disposable artifact.
USER_CFLAGS := -O2 -Wall -Wextra -march=rv64gc -mabi=lp64 -mcmodel=medany \
  -static -nostdlib -nostartfiles -ffreestanding -fno-pic -mno-relax \
  -fno-stack-protector

user/_fstest: user/fstest.c user/entry.S user/user.h kernel/user.ld
	mkdir -p user
	$(RISCV_CC) $(USER_CFLAGS) -I kernel -T kernel/user.ld \
		user/entry.S user/fstest.c -o $@

user-fstest: user/_fstest
	@echo "+ $<"

clean:
	rm -f $(K)/*.o $(K)/*.d kernel/kernel
	rm -f mkfs/mkfs mkfs/mkfs.exe
	rm -f fs.img
	rm -f user/_fstest
	rm -rf $(LAB1_BUILD)
