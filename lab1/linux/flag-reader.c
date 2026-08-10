// Lab 1 CTF warm-up: Linux flags reader.
//
// Demonstrates the operating-system boundary by reading the same two generated
// non-secret fixture files through ordinary Linux file operations and emitting
// their bounded hashed provenance records in stable alternating order. No
// expected value (real or generated) is embedded in this source.
//
// Usage: flag-reader <directory>
//   directory must contain regular files named flag1 and flag2.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sha256.h"

// Static processing bounds used to satisfy the ctf-bounded-processing invariant.
#define MAX_NAME 8
#define MAX_CONTENT 4096
#define NUM_FILES 2

static const char *const FILE_NAMES[NUM_FILES] = {"flag1", "flag2"};

// Reads one file fully into buf (capacity cap). Returns 1 on success and stores
// the byte count in *out_len, 0 on any error. Never prints file contents.
static int read_one_file(const char *path, uint8_t *buf, size_t cap, size_t *out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "flag-reader: open failed for %s: %s\n", path, strerror(errno));
        return 0;
    }
    size_t used = 0;
    for (;;) {
        ssize_t n = read(fd, buf + used, cap - used);
        if (n < 0) {
            fprintf(stderr, "flag-reader: read failed for %s: %s\n", path, strerror(errno));
            close(fd);
            return 0;
        }
        if (n == 0) break;
        used += (size_t)n;
        if (used >= cap) {
            fprintf(stderr, "flag-reader: %s exceeds the bounded buffer\n", path);
            close(fd);
            return 0;
        }
    }
    if (close(fd) != 0) {
        fprintf(stderr, "flag-reader: close failed for %s: %s\n", path, strerror(errno));
        return 0;
    }
    *out_len = used;
    return 1;
}

static int emit_record(const char *name, const uint8_t *buf, size_t len) {
    uint8_t digest[32];
    char hex[65];
    vos_sha256(buf, len, digest);
    for (int i = 0; i < 32; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    hex[64] = '\0';
    if (printf("CTF_RECORD name=%s length=%zu sha256=%s\n", name, len, hex) < 0) {
        fprintf(stderr, "flag-reader: output failed\n");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: flag-reader <directory containing flag1 and flag2>\n");
        return 2;
    }
    const char *dir = argv[1];
    char path[8 + 1 + 256];
    static uint8_t content[NUM_FILES][MAX_CONTENT];

    for (int i = 0; i < NUM_FILES; i++) {
        if (snprintf(path, sizeof(path), "%s/%s", dir, FILE_NAMES[i]) >= (int)sizeof(path)) {
            fprintf(stderr, "flag-reader: path too long for %s\n", FILE_NAMES[i]);
            return 1;
        }
        size_t len = 0;
        if (!read_one_file(path, content[i], MAX_CONTENT, &len)) return 1;
        if (!emit_record(FILE_NAMES[i], content[i], len)) return 1;
    }
    return 0;
}
