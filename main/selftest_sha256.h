/*
 * selftest_sha256.h — a small SHA-256 (FIPS 180-4) for the flash check (TASKS.md STARK-0707).
 * Pure: the check hashes the running image through plain reads, so it needs neither the hardware
 * accelerator (which the emulator does not model) nor mbedTLS (which nothing else here links).
 * Host-tested against the FIPS vectors.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint8_t block[64];
    uint64_t bytes;
    size_t fill;
} selftest_sha256_t;

void selftest_sha256_init(selftest_sha256_t *c);
void selftest_sha256_update(selftest_sha256_t *c, const void *data, size_t n);
void selftest_sha256_final(selftest_sha256_t *c, uint8_t out[32]);
