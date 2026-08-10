// Self-contained SHA-256 implementation shared by the Lab 1 CTF readers.
#include "sha256.h"

#include <string.h>

static const uint32_t SHA_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIG0(x) (ROR32((x), 2) ^ ROR32((x), 13) ^ ROR32((x), 22))
#define SIG1(x) (ROR32((x), 6) ^ ROR32((x), 11) ^ ROR32((x), 25))
#define GAM0(x) (ROR32((x), 7) ^ ROR32((x), 18) ^ ((x) >> 3))
#define GAM1(x) (ROR32((x), 17) ^ ROR32((x), 19) ^ ((x) >> 10))

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void be32put(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void be64put(uint8_t *p, uint64_t v) {
    for (int k = 0; k < 8; k++) p[7 - k] = (uint8_t)(v >> (8 * k));
}

static void sha256_block(uint32_t h[8], const uint8_t *block) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = be32(block + i * 4);
    for (int i = 16; i < 64; i++) w[i] = GAM1(w[i - 2]) + w[i - 7] + GAM0(w[i - 15]) + w[i - 16];

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + SIG1(e) + CH(e, f, g) + SHA_K[i] + w[i];
        uint32_t t2 = SIG0(a) + MAJ(a, b, c);
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void vos_sha256(const uint8_t *msg, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    // Process complete 64-byte message blocks.
    size_t full = len & ~(size_t)63u;
    for (size_t i = 0; i < full; i += 64) sha256_block(h, msg + i);

    // Remaining bytes and the required padding into a single block buffer.
    uint8_t block[64];
    size_t rem = len - full;
    memcpy(block, msg + full, rem);
    block[rem] = 0x80;
    if (rem < 56) {
        memset(block + rem + 1, 0, 56 - rem - 1);
        be64put(block + 56, (uint64_t)len * 8);
        sha256_block(h, block);
    } else {
        memset(block + rem + 1, 0, 64 - rem - 1);
        sha256_block(h, block);
        uint8_t pad[64];
        memset(pad, 0, 64);
        be64put(pad + 56, (uint64_t)len * 8);
        sha256_block(h, pad);
    }

    for (int i = 0; i < 8; i++) be32put(out + i * 4, h[i]);
}
