#ifndef BLOOM_H
#define BLOOM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *bits;      /* bit array */
    size_t   m_bits;    /* number of bits */
    size_t   k_hashes;  /* number of hash functions */
    size_t   n_inserted; /* number of inserted items */
    uint64_t seed;      /* random seed */
} BloomFilter;

int bloom_init_mk(BloomFilter *bf,
                  size_t m_bits,
                  size_t k_hashes);

void bloom_free(BloomFilter *bf);/*  */
void bloom_clear(BloomFilter *bf);

void bloom_add(BloomFilter *bf,
               const void *key,
               size_t len);

int bloom_maybe_contains(const BloomFilter *bf,
                         const void *key,
                         size_t len);

/* Optional helper: estimate false positive probability after inserting n items */
double bloom_estimate_fp(const BloomFilter *bf, size_t n_inserted);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_H */
