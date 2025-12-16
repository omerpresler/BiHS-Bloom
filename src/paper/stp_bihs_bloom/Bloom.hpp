#ifndef BLOOM_H
#define BLOOM_H

#include <stddef.h>
#include <stdint.h>
#include <set>


class BloomFilter {
public:
    BloomFilter(size_t m_bits, size_t k_hashes);
    ~BloomFilter();

    // Delete copy constructor and assignment to avoid deep copy issues for now
    BloomFilter(const BloomFilter&) = delete;
    BloomFilter& operator=(const BloomFilter&) = delete;

    void clear();
    void add(const void *key, size_t len);
    bool maybe_contains(const void *key, size_t len) const;
    double estimate_fp() const;

    size_t get_n_inserted() const { return n_inserted; }

private:
    uint8_t *bits;      /* bit array */
    size_t   m_bits;    /* number of bits */
    size_t   k_hashes;  /* number of hash functions */
    size_t   n_inserted; /* number of inserted items */
    uint64_t seed;      /* random seed */

    void hashes(const void *key, size_t len, uint64_t *h1, uint64_t *h2) const;
};


template <typename Env>
class BloomFilterWithSet : public BloomFilter {
public:
    BloomFilterWithSet(size_t bloom_bits, size_t set_limit, size_t k_hashes, Env env = Env{})
        : BloomFilter(bloom_bits, k_hashes),
          set_limit(set_limit),
          env(std::move(env)) {}

    void add(const void *key, size_t len) {
        if (set.size() < set_limit) {
            set.insert(get_hash(key));
        }
        BloomFilter::add(key, len);
    }

    bool maybe_contains(const void *key, size_t len) const {
        if (set.size() >= set_limit) {
            return BloomFilter::maybe_contains(key, len);
        }
        const uint64_t h = get_hash(key);
        return set.find(h) != set.end();
    }

    void clear() {
        BloomFilter::clear();
        set.clear();
    }

private:
    uint64_t get_hash(const void *key) const {
        // Expects Env to provide: uint64_t GetStateHash(const void*) const;
        return static_cast<uint64_t>(env.GetStateHash(key));
    }

    std::set<uint64_t> set;
    size_t set_limit;
    Env env;
};



#endif /* BLOOM_H */
