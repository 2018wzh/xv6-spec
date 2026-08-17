K=kernel
TOOLPREFIX ?= $(shell if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then echo riscv64-unknown-elf-; elif command -v riscv64-elf-gcc >/dev/null 2>&1; then echo riscv64-elf-; elif command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then echo riscv64-linux-gnu-; else echo riscv64-unknown-elf-; fi)
CC = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -std=gnu99 -march=rv64gc -mabi=lp64 -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I.
ASFLAGS = -march=rv64gc -mabi=lp64
LDFLAGS = -m elf64lriscv
OBJS = kernel/bio.o \
  kernel/boot.o \
  kernel/console.o \
  kernel/entry.o \
  kernel/exec.o \
  kernel/file.o \
  kernel/fs.o \
  kernel/kalloc.o \
  kernel/kernelvec.o \
  kernel/log.o \
  kernel/main.o \
  kernel/plic.o \
  kernel/printk.o \
  kernel/proc.o \
  kernel/sleeplock.o \
  kernel/spinlock.o \
  kernel/start.o \
  kernel/string.o \
  kernel/swtch.o \
  kernel/syscall.o \
  kernel/sysproc.o \
  kernel/trampoline.o \
  kernel/trap.o \
  kernel/uart.o \
  kernel/virtio_disk.o \
  kernel/vm.o

all: $(K)/kernel

$(K)/kernel: $(OBJS) $(K)/kernel.ld
	$(LD) $(LDFLAGS) -z max-page-size=4096 -T $(K)/kernel.ld -o $@ $(OBJS)

qemu: $(K)/kernel
	qemu-system-riscv64 -machine virt -bios none -kernel $(K)/kernel -m 128M -smp 1 -nographic

clean:
	rm -f $(K)/*.o $(K)/*.d $(K)/kernel
