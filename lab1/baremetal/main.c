// Lab 1 CTF warm-up: freestanding RISC-V guest reader.
//
// Runs as the kernel image on the QEMU RISC-V virt machine. The generated
// non-secret fixture image is linked into this ELF as a read-only blob; the
// guest parses its directory, reads both bounded values, and emits the same
// hashed provenance records the Linux reader produces, plus the completion
// marker the toolchain serial oracle accepts. No expected value is embedded.

#include <stdint.h>

#include "sha256.h"
#include "uart.h"

extern void *vos_memcpy(void *dst, const void *src, size_t n);
extern int vos_memcmp(const void *a, const void *b, size_t n);

// The embedded fixture image blob (created by objcopy from lab1/build/flags.img).
extern const uint8_t _binary_flags_img_start[];
extern const uint8_t _binary_flags_img_end[];

// QEMU virt power-off (riscv-test / sifive_test) device.
#define TEST_DEVICE 0x100000UL
#define TEST_PASS 0x5555UL
#define TEST_FAIL 0x3333UL

#define MAX_IMAGE 4096
#define MAX_ENTRIES 16
#define MAX_ENTRY_SIZE 64
#define MAX_NAME 8
#define MAX_CONTENT 4096
#define RECORDS_EXPECTED 2

// Little-endian u32 loads from a byte array (bounds-checked by callers).
static uint32_t ld_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void power_off(int ok) {
    volatile uint32_t *test = (volatile uint32_t *)TEST_DEVICE;
    *test = ok ? TEST_PASS : TEST_FAIL;
    // If the device is absent, never return to a live loop; spin forever.
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static void fail(const char *reason) {
    uart_puts("\nCTF_BAREMETAL_FAIL: ");
    uart_puts(reason);
    uart_puts("\n");
    power_off(0);
}

static void print_u32(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) uart_putc(buf[--i]);
}

static void print_hex(const uint8_t *digest, size_t len) {
    static const char *const hex = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        uart_putc(hex[digest[i] >> 4]);
        uart_putc(hex[digest[i] & 0x0f]);
    }
}

// Emit one CTF_RECORD for a file described by content at offset with length.
static void emit_record(const char *name, const uint8_t *content, size_t len) {
    uint8_t digest[32];
    vos_sha256(content, len, digest);
    uart_puts("CTF_RECORD name=");
    uart_puts(name);
    uart_puts(" length=");
    print_u32((uint32_t)len);
    uart_puts(" sha256=");
    print_hex(digest, 32);
    uart_puts("\n");
}

int main(void) {
    uart_init();

    const uint8_t *img = _binary_flags_img_start;
    const size_t img_size = (size_t)(_binary_flags_img_end - _binary_flags_img_start);

    // Redacted metadata: emit nothing secret, only the fixture marker.
    uart_puts("ctf-warmup baremetal reader reading embedded generated image\n");

    // Validate magic.
    if (img_size < 28) fail("image too small for header");
    if (vos_memcmp(img, "VOSCTF1\0", 8) != 0) fail("bad image magic");

    uint32_t entry_count = ld_le32(img + 8);
    uint32_t dir_offset = ld_le32(img + 12);
    uint32_t entry_size = ld_le32(img + 16);
    uint32_t sector_size = ld_le32(img + 20);
    uint32_t image_size = ld_le32(img + 24);

    if (entry_count == 0 || entry_count > MAX_ENTRIES) fail("invalid entry count");
    if (entry_size == 0 || entry_size > MAX_ENTRY_SIZE) fail("invalid entry size");
    if (sector_size == 0) fail("invalid sector size");
    if (image_size != img_size || image_size > MAX_IMAGE) fail("image size mismatch");
    if (dir_offset == 0 || dir_offset + entry_count * entry_size > image_size) {
        fail("directory out of bounds");
    }

    const uint8_t *found[RECORDS_EXPECTED] = {0, 0};
    size_t found_len[RECORDS_EXPECTED] = {0, 0};
    static const char *const WANT[RECORDS_EXPECTED] = {"flag1", "flag2"};

    for (uint32_t e = 0; e < entry_count; e++) {
        const uint8_t *ent = img + dir_offset + (size_t)e * entry_size;
        char name[MAX_NAME + 1];
        size_t nlen = 0;
        for (size_t i = 0; i < MAX_NAME; i++) {
            if (ent[i] == 0) break;
            name[nlen++] = (char)ent[i];
        }
        name[nlen] = '\0';

        uint32_t data_off = ld_le32(ent + 8);
        uint32_t data_len = ld_le32(ent + 12);
        if ((uint64_t)data_off + data_len > img_size) fail("entry data out of bounds");
        if (data_len == 0 || data_len > MAX_CONTENT) fail("oversized entry");

        for (int w = 0; w < RECORDS_EXPECTED; w++) {
            size_t want_len = 0;
            while (want_len < MAX_NAME && WANT[w][want_len] != '\0') want_len++;
            if (found[w] == 0 && want_len == nlen &&
                vos_memcmp(name, WANT[w], nlen) == 0) {
                const uint8_t *content = img + data_off;
                found[w] = content;
                found_len[w] = data_len;
            }
        }
    }

    for (int w = 0; w < RECORDS_EXPECTED; w++) {
        if (found[w] == 0) {
            uart_puts("missing entry ");
            uart_puts(WANT[w]);
            uart_puts("\n");
            fail("missing required entry");
        }
    }

    for (int w = 0; w < RECORDS_EXPECTED; w++) {
        emit_record(WANT[w], found[w], found_len[w]);
    }

    uart_puts("CTF_BAREMETAL_OK\n");
    power_off(1);
    return 0;
}
