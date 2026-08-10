CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11

RISCV_CC ?= riscv64-linux-gnu-gcc
RISCV_OBJCOPY ?= riscv64-linux-gnu-objcopy
RISCV_CFLAGS ?= -O2 -Wall -Wextra -march=rv64gc -mabi=lp64 -mcmodel=medany -static -nostdlib -nostartfiles -ffreestanding

BUN ?= bun

BUILD := lab1/build
SEED := 0x5eed0001

LINUX_SRCS := lab1/linux/flag-reader.c lab1/linux/sha256.c
BM_SRCS := lab1/baremetal/start.S lab1/baremetal/uart.c lab1/baremetal/sha256.c lab1/baremetal/support.c lab1/baremetal/main.c

.PHONY: all qemu clean gen-fixture

all: $(BUILD)/flag-reader $(BUILD)/ctf-baremetal.elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/.dir-stamp: | $(BUILD)
	@touch $@

# Generate the deterministic non-secret fixture into a held directory.
gen-fixture: | $(BUILD)/.dir-stamp
	$(BUN) tests/public/ctf-fixture.ts generate $(BUILD)/fixture $(SEED)
	test -f $(BUILD)/fixture/flags.img
	test -f $(BUILD)/fixture/metadata.json

$(BUILD)/flag-reader: $(LINUX_SRCS) lab1/linux/sha256.h | $(BUILD)/.dir-stamp
	$(CC) $(CFLAGS) -I lab1/linux -o $@ lab1/linux/flag-reader.c lab1/linux/sha256.c

$(BUILD)/flags.img: gen-fixture
	cp $(BUILD)/fixture/flags.img $@

$(BUILD)/flags_img.o: $(BUILD)/flags.img
	cd $(BUILD) && $(RISCV_OBJCOPY) -I binary -O elf64-littleriscv -B riscv flags.img flags_img.o

$(BUILD)/ctf-baremetal.elf: $(BM_SRCS) lab1/baremetal/sha256.h lab1/baremetal/uart.h lab1/baremetal/linker.ld $(BUILD)/flags_img.o | $(BUILD)/.dir-stamp
	$(RISCV_CC) $(RISCV_CFLAGS) -I lab1/baremetal -T lab1/baremetal/linker.ld \
		$(BM_SRCS) $(BUILD)/flags_img.o -o $@

qemu: $(BUILD)/ctf-baremetal.elf
	@mkdir -p $(BUILD)
	@set -o pipefail; \
	qemu-system-riscv64 -machine virt -m 128M -nographic -no-reboot -bios none \
		-kernel $(BUILD)/ctf-baremetal.elf > $(BUILD)/baremetal.log 2>&1; \
	if grep -q 'CTF_BAREMETAL_OK' $(BUILD)/baremetal.log; then \
		echo "qemu: completion marker observed"; \
	else \
		echo "qemu: missing CTF_BAREMETAL_OK marker" >&2; \
		exit 1; \
	fi

clean:
	rm -rf $(BUILD)
	rm -f dir-stamp
