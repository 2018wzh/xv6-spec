// Shared self-contained SHA-256 used by the Lab 1 CTF readers.
#ifndef VOS_LAB1_SHA256_H
#define VOS_LAB1_SHA256_H

#include <stddef.h>
#include <stdint.h>

// Computes the SHA-256 digest of msg[0..len) into out (exactly 32 bytes).
void vos_sha256(const uint8_t *msg, size_t len, uint8_t out[32]);

#endif
