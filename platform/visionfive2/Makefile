.DEFAULT_GOAL := all

K=platform/visionfive2/kernel
U=platform/visionfive2/user

OBJS = \
  $K/entry.o \
  $K/boot.o \
  $K/platform.o \
  $K/sbi.o \
  $K/start.o \
  $K/console.o \
  $K/printk.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/blockdev.o \
  $K/sd.o \
  $K/gpt.o \
  $K/virtio_disk.o

PLATFORM ?= qemu-virt
KERNEL_LDSCRIPT = $K/kernel.ld
ifeq ($(PLATFORM),visionfive2)
PLATFORM_CFLAGS = -DPLATFORM_VISIONFIVE2
OBJS += third_party/libfdt/fdt.o third_party/libfdt/fdt_ro.o
KERNEL_LDSCRIPT = $K/kernel-vf2.ld
endif

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX = 

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-elf-'; \
	elif riscv64-none-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-none-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

HOST_CC ?= gcc
HOST_CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
QEMU = qemu-system-riscv64
MIN_QEMU_VERSION = 7.2

CC = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -Wno-unused-but-set-variable
CFLAGS += -march=rv64gc
CFLAGS += -std=gnu99
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding
CFLAGS += -fno-common -nostdlib
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free
CFLAGS += -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -Iplatform/visionfive2/kernel -Iplatform/visionfive2 -Ithird_party/libfdt -Ithird_party
CFLAGS += $(PLATFORM_CFLAGS)
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

PLATFORM_STAMP = build/.platform-vf2-$(PLATFORM)

$(PLATFORM_STAMP): | build
	rm -f build/.platform-vf2-* $K/*.o $K/*.d $U/*.o $U/*.d \
	  third_party/libfdt/*.o third_party/libfdt/*.d
	touch $@

$(OBJS): $(PLATFORM_STAMP)

$K/kernel: $(OBJS) $(KERNEL_LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(KERNEL_LDSCRIPT) -o $K/kernel $(OBJS)
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

$K/%.o: $K/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

tags: $(OBJS)
	etags kernel/*.S kernel/*.c

ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

_%: %.o $(ULIB) $U/user.ld
	$(LD) $(LDFLAGS) -T $U/user.ld -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S

$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

$U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

VENDOR_MKFS := platform/visionfive2/mkfs/mkfs
$(VENDOR_MKFS): platform/visionfive2/mkfs/mkfs.c $K/fs.h $K/param.h
	$(HOST_CC) $(HOST_CFLAGS) -Wno-unknown-attributes -Iplatform/visionfive2 -o $(VENDOR_MKFS) platform/visionfive2/mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_logstress\
	$U/_forphan\
	$U/_dorphan\

build/vf2-fs.img: $(VENDOR_MKFS) platform/visionfive2/XV6-README $(UPROGS)
	tmp=README; trap 'rm -f $$tmp' EXIT; cp platform/visionfive2/XV6-README $$tmp; $(VENDOR_MKFS) build/vf2-fs.img $$tmp $(UPROGS)

all: build/kernel-vf2.elf build/kernel-vf2.bin build/kernel-vf2.asm build/vf2-fs.img

build:
	mkdir -p build

build/kernel-vf2.elf: $K/kernel | build
	cp $K/kernel $@

build/kernel-vf2.bin: build/kernel-vf2.elf | build
	$(OBJCOPY) -O binary $< $@

build/kernel-vf2.asm: build/kernel-vf2.elf | build
	$(OBJDUMP) -D $< > $@

-include kernel/*.d user/*.d

clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	$K/*.o $K/*.d $K/*.asm $K/*.sym $K/kernel \
	$U/*.o $U/*.d $U/*.asm $U/*.sym $U/usys.S \
	$(UPROGS) \
	build/kernel-vf2.elf build/kernel-vf2.bin build/kernel-vf2.asm \
	build/vf2-fs.img build/visionfive2.dtb build/xv6.itb build/visionfive2-sd.img \
	build/.platform-vf2-* \
	third_party/libfdt/*.o third_party/libfdt/*.d \
	platform/visionfive2/mkfs/mkfs .gdbinit

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 4
endif

qemu: check-qemu-version $K/kernel build/vf2-fs.img
	$(QEMU) -machine virt -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic \
		-global virtio-mmio.force-legacy=false \
		-drive file=build/vf2-fs.img,if=none,format=raw,id=x0 \
		-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $K/kernel .gdbinit build/vf2-fs.img
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

print-gdbport:
	@echo $(GDBPORT)

QEMU_VERSION := $(shell $(QEMU) --version | head -n 1 | sed -E 's/^QEMU emulator version ([0-9]+\.[0-9]+)\..*/\1/')
check-qemu-version:
	@if [ "$(shell echo "$(QEMU_VERSION) >= $(MIN_QEMU_VERSION)" | bc)" -eq 0 ]; then \
		echo "ERROR: Need qemu version >= $(MIN_QEMU_VERSION)"; \
		exit 1; \
	fi

.PHONY: fmt qemu qemu-gdb
fmt:
	clang-format -i $(wildcard kernel/*.[ch] user/*.[ch] mkfs/*.c)
