#include "Bloom.hpp"
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
static uint64_t fnv1a_64(const void *key,size_t len, uint64_t seed)
{
    const uint8_t *data = (const uint8_t *)key;
    uint64_t hash = 1469598103934665603ULL ^ seed;

    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

BloomFilter::BloomFilter(size_t m_bits, size_t k_hashes)
    : m_bits(m_bits), k_hashes(k_hashes), n_inserted(0)
{
    if (m_bits == 0 || k_hashes == 0) {
        bits = nullptr;
        return;
    }

    size_t bytes = (m_bits + 7) / 8;
    bits = (uint8_t *)calloc(bytes, 1);
    
    /* Initialize random seed */
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&seed, sizeof(seed), 1, f) != 1) {
            seed = (uint64_t)time(NULL) ^ (uintptr_t)this;
        }
        fclose(f);
    } else {
        seed = (uint64_t)time(NULL) ^ (uintptr_t)this;
    }
}

BloomFilter::~BloomFilter()
{
    if (bits) {
        free(bits);
        bits = nullptr;
    }
}

void BloomFilter::clear()
{
    if (!bits) return;
    size_t bytes = (m_bits + 7) / 8;
    memset(bits, 0, bytes);
    n_inserted = 0;
}

void BloomFilter::hashes(const void *key, size_t len, uint64_t *h1, uint64_t *h2) const
{
    *h1 = fnv1a_64(key, len, 0xA5A5A5A5A5A5A5A5ULL ^ seed);
    *h2 = fnv1a_64(key, len, 0x5A5A5A5A5A5A5A5AULL ^ seed);
    if (*h2 == 0) {
        *h2 = 0x27d4eb2d; /* avoid zero step */
    }
}

void BloomFilter::add(const void *key, size_t len)
{
    if (!bits || m_bits == 0 || k_hashes == 0) return;

    uint64_t h1, h2;
    hashes(key, len, &h1, &h2);

    for (size_t i = 0; i < k_hashes; i++) {
        uint64_t h = h1 + i * h2;
        size_t idx = (size_t)(h % m_bits);
        set_bit(bits, idx);
    }
    n_inserted++;
}

bool BloomFilter::maybe_contains(const void *key, size_t len) const
{
    if (!bits || m_bits == 0 || k_hashes == 0) return false;

    uint64_t h1, h2;
    hashes(key, len, &h1, &h2);

    for (size_t i = 0; i < k_hashes; i++) {
        uint64_t h = h1 + i * h2;
        size_t idx = (size_t)(h % m_bits);
        if (!get_bit(bits, idx)) {
            return false; /* definitely not present */
        }
    }
    return true; /* may be present */
}

double BloomFilter::estimate_fp() const
{
    if (m_bits == 0 || k_hashes == 0) return 1.0;

    double m = (double)m_bits;
    double k = (double)k_hashes;
    double n = (double)n_inserted;

    /* p ≈ (1 - e^{-k n / m})^k */
    double exponent = -k * n / m;
    double p1 = 1.0 - exp(exponent);
    if (p1 < 0.0) p1 = 0.0;
    if (p1 > 1.0) p1 = 1.0;
    return pow(p1, k);
}
