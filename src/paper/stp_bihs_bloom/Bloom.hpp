#ifndef BLOOM_H
#define BLOOM_H

#include <stddef.h>
#include <stdint.h>
#include <set>

#include "MNPuzzle.h"

#define MN_SIZE 4 

enum class BloomType {
    REGULAR,
    WITH_SET
};

template <typename Key>
class BloomFilter {
public:
    BloomFilter(size_t m_bits, size_t k_hashes) : m_bits(m_bits), k_hashes(k_hashes), n_inserted(0)
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
    ~BloomFilter()
    {
        if (bits) {
            free(bits);
            bits = nullptr;
        }
    }

    // Delete copy constructor and assignment to avoid deep copy issues for now
    BloomFilter(const BloomFilter&) = delete;
    BloomFilter& operator=(const BloomFilter&) = delete;

    virtual void clear()
    {
        if (!bits) return;
        size_t bytes = (m_bits + 7) / 8;
        memset(bits, 0, bytes);
        n_inserted = 0;
    }
    virtual void add(const Key &key)
    {
        if (!bits || m_bits == 0 || k_hashes == 0) return;

        uint64_t h1, h2;
        hashes(key, &h1, &h2);

        for (size_t i = 0; i < k_hashes; i++) {
            uint64_t h = h1 + i * h2;
            size_t idx = (size_t)(h % m_bits);
            set_bit(bits, idx);
        }
        n_inserted++;
    }
    virtual bool maybe_contains(const Key &key) const
    {
        if (!bits || m_bits == 0 || k_hashes == 0) return false;

        uint64_t h1, h2;
        hashes(key, &h1, &h2);

        for (size_t i = 0; i < k_hashes; i++) {
            uint64_t h = h1 + i * h2;
            size_t idx = (size_t)(h % m_bits);
            if (!get_bit(bits, idx)) {
                return false; /* definitely not present */
            }
        }
        return true; /* may be present */
    }
    double estimate_fp() const
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

    size_t get_n_inserted() const { return n_inserted; }

protected:
    uint8_t *bits;      /* bit array */
    size_t   m_bits;    /* number of bits */
    size_t   k_hashes;  /* number of hash functions */
    size_t   n_inserted; /* number of inserted items */
    uint64_t seed;      /* random seed */

    void hashes(const Key &key, uint64_t *h1, uint64_t *h2) const
    {
        *h1 = fnv1a_64(key, 0xA5A5A5A5A5A5A5A5ULL ^ seed);
        *h2 = fnv1a_64(key, 0x5A5A5A5A5A5A5A5AULL ^ seed);
        if (*h2 == 0) {
            *h2 = 0x27d4eb2d; /* avoid zero step */
        }
    }

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
    static uint64_t fnv1a_64(const Key &key, uint64_t seed)
    {
        const uint8_t* data =
        reinterpret_cast<const uint8_t*>(key.data());

        constexpr size_t byteCount =
            sizeof(key[0]) * key.size();

        uint64_t hash = 1469598103934665603ULL ^ seed;

        for (size_t i = 0; i < byteCount; i++) {
            hash ^= static_cast<uint64_t>(data[i]);
            hash *= 1099511628211ULL;
        }

        return hash;
    }
};


template <typename Env, typename Key>
class BloomFilterWithSet : public BloomFilter<Key> {
public:
    BloomFilterWithSet(size_t bloom_bits, size_t set_limit, size_t k_hashes, Env env = Env{})
        : BloomFilter<Key>(bloom_bits, k_hashes),
          set_limit(set_limit),
          env(std::move(env)) {}
            
    void add(const Key &key) override {
        if (set.size() < set_limit) {
            MNPuzzleState<MN_SIZE, MN_SIZE> state;
            state.puzzle = key;
            set.insert(env.GetStateHash(state));
        }
        BloomFilter<Key>::add(key);
    }

    bool maybe_contains(const Key &key) const override {
        if (set.size() >= set_limit) {
            return BloomFilter<Key>::maybe_contains(key);
        }
        MNPuzzleState<MN_SIZE, MN_SIZE> state;
        state.puzzle = key;
        const uint64_t h = env.GetStateHash(state);
        return set.find(h) != set.end();
    }

    void clear() override {
        BloomFilter<Key>::clear();
        set.clear();
    }
    size_t get_set_limit() const { return set_limit; }
    size_t get_set_size() const { return set.size(); }

private:
/*
    uint64_t get_hash(const void *key) const {
        const MNPuzzleState<MN_SIZE, MN_SIZE> *state = static_cast<const MNPuzzleState<MN_SIZE, MN_SIZE> *>(key);
        return static_cast<uint64_t>(env.GetStateHash(*state));
    }
*/

    std::set<uint64_t> set;
    size_t set_limit;
    Env env;
};



#endif /* BLOOM_H */
