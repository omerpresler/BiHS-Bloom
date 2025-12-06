#include "Bloom.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdio.h>

/* bit helpers */
static inline void set_bit(uint8_t *bits, size_t idx)
{
    bits[idx >> 3] |= (uint8_t)(1u << (idx & 7u));
}

static inline int get_bit(const uint8_t *bits, size_t idx)
{
    return (bits[idx >> 3] >> (idx & 7u)) & 1u;
}

/* FNV-1a 64-bit hash */
static uint64_t fnv1a_64(const void *key, size_t len, uint64_t seed)
{
    const uint8_t *data = (const uint8_t *)key;
    uint64_t hash = 1469598103934665603ULL ^ seed;

    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* Kirsch–Mitzenmacher two-base-hashes trick */
static void bloom_hashes(const void *key, size_t len, uint64_t seed,
                         uint64_t *h1, uint64_t *h2)
{
    *h1 = fnv1a_64(key, len, 0xA5A5A5A5A5A5A5A5ULL ^ seed);
    *h2 = fnv1a_64(key, len, 0x5A5A5A5A5A5A5A5AULL ^ seed);
    if (*h2 == 0) {
        *h2 = 0x27d4eb2d; /* avoid zero step */
    }
}

/* NEW: explicit m and k initializer */
int bloom_init_mk(BloomFilter *bf,
                  size_t m_bits,
                  size_t k_hashes)
{
    if (!bf || m_bits == 0 || k_hashes == 0) {
        return -1;
    }

    size_t bytes = (m_bits + 7) / 8;
    uint8_t *bits = (uint8_t *)calloc(bytes, 1);
    if (!bits) {
        return -1;
    }

    bf->bits     = bits;
    bf->m_bits   = m_bits;
    bf->k_hashes = k_hashes;
    bf->k_hashes = k_hashes;
    bf->n_inserted = 0;

    /* Initialize random seed */
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&bf->seed, sizeof(bf->seed), 1, f) != 1) {
            bf->seed = (uint64_t)time(NULL) ^ (uintptr_t)bf;
        }
        fclose(f);
    } else {
        bf->seed = (uint64_t)time(NULL) ^ (uintptr_t)bf;
    }

    return 0;
}

void bloom_free(BloomFilter *bf)
{
    if (!bf) return;
    free(bf->bits);
    bf->bits     = NULL;
    bf->m_bits   = 0;
    bf->k_hashes = 0;
}

void bloom_clear(BloomFilter *bf)
{
    if (!bf || !bf->bits) return;
    size_t bytes = (bf->m_bits + 7) / 8;
    memset(bf->bits, 0, bytes);
    bf->n_inserted = 0;
}

void bloom_add(BloomFilter *bf,
               const void *key,
               size_t len)
{
    if (!bf || !bf->bits || bf->m_bits == 0 || bf->k_hashes == 0) return;

    uint64_t h1, h2;
    bloom_hashes(key, len, bf->seed, &h1, &h2);

    for (size_t i = 0; i < bf->k_hashes; i++) {
        uint64_t h = h1 + i * h2;
        size_t idx = (size_t)(h % bf->m_bits);
        set_bit(bf->bits, idx);
    }
    bf->n_inserted++;
}

int bloom_maybe_contains(const BloomFilter *bf,
                         const void *key,
                         size_t len)
{
    if (!bf || !bf->bits || bf->m_bits == 0 || bf->k_hashes == 0) return 0;

    uint64_t h1, h2;
    bloom_hashes(key, len, bf->seed, &h1, &h2);

    for (size_t i = 0; i < bf->k_hashes; i++) {
        uint64_t h = h1 + i * h2;
        size_t idx = (size_t)(h % bf->m_bits);
        if (!get_bit(bf->bits, idx)) {
            return 0; /* definitely not present */
        }
    }
    return 1; /* may be present */
}

/* Optional: estimate false positive probability after inserting n items */
double bloom_estimate_fp(const BloomFilter *bf, size_t n_inserted)
{
    if (!bf || bf->m_bits == 0 || bf->k_hashes == 0) return 1.0;

    double m = (double)bf->m_bits;
    double k = (double)bf->k_hashes;
    double n = (double)n_inserted;

    /* p ≈ (1 - e^{-k n / m})^k */
    double exponent = -k * n / m;
    double p1 = 1.0 - exp(exponent);
    if (p1 < 0.0) p1 = 0.0;
    if (p1 > 1.0) p1 = 1.0;
    return pow(p1, k);
}
