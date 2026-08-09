K=kernel
TOOLPREFIX ?= $(shell if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then echo riscv64-unknown-elf-; elif command -v riscv64-elf-gcc >/dev/null 2>&1; then echo riscv64-elf-; elif command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then echo riscv64-linux-gnu-; else echo riscv64-unknown-elf-; fi)
CC = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -std=gnu99 -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I.
OBJS = kernel/boot.o \
  kernel/entry.o \
  kernel/main.o \
  kernel/start.o \
  kernel/string.o

all: $(K)/kernel

$(K)/kernel: $(OBJS) $(K)/kernel.ld
	$(LD) -z max-page-size=4096 -T $(K)/kernel.ld -o $@ $(OBJS)

qemu: $(K)/kernel
	qemu-system-riscv64 -machine virt -bios none -kernel $(K)/kernel -m 128M -smp 1 -nographic

clean:
	rm -f $(K)/*.o $(K)/*.d $(K)/kernel
