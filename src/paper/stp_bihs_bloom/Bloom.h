#ifndef BLOOM_H
#define BLOOM_H

#include <stddef.h>
#include <stdint.h>
#include <unordered_set>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "MNPuzzle.h"
#include "PancakePuzzle.h"

#define MN_SIZE 4

template <typename Key>
class BloomFilter {
public:
    BloomFilter(size_t m_bits, size_t k_hashes) : m_bits(m_bits), k_hashes(k_hashes), n_inserted(0), bits_set_count(0)
    {
        if (m_bits == 0 || k_hashes == 0) {
            bits = nullptr;
            return;
        }

        size_t bytes = (m_bits + 7) / 8;
        bits = (uint8_t *)calloc(bytes, 1);
        
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
    virtual ~BloomFilter()
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
        bits_set_count = 0;
        //unique_set.clear();
    }



    virtual void add(const Key &key)
    {
        add_hash(stable_fingerprint(key));
    }

    void add_hash(uint64_t fingerprint)
    {
        if (!bits || m_bits == 0 || k_hashes == 0) return;

        uint64_t h1, h2;
        hashes_from_fingerprint(fingerprint, &h1, &h2);

        for (size_t i = 0; i < k_hashes; i++) {
            uint64_t h = h1 + i * h2;
            size_t idx = (size_t)(mix64(h) % m_bits);
            if (!get_bit(bits, idx)) {
                bits_set_count++;
            }
            set_bit(bits, idx);
        }
        n_inserted++;
        //unique_set.insert(fingerprint);
    }

    size_t get_n_unique() const { return n_inserted; } // exact unique tracking is disabled
    virtual bool maybe_contains(const Key &key) const
    {
        return maybe_contains_hash(stable_fingerprint(key));
    }

    bool maybe_contains_hash(uint64_t fingerprint) const
    {
        if (!bits || m_bits == 0 || k_hashes == 0) return false;

        uint64_t h1, h2;
        hashes_from_fingerprint(fingerprint, &h1, &h2);

        for (size_t i = 0; i < k_hashes; i++) {
            uint64_t h = h1 + i * h2;
            size_t idx = (size_t)(mix64(h) % m_bits);
            if (!get_bit(bits, idx)) {
                return false; /* definitely not present */
            }
        }
        return true; /* may be present */
    }

    static uint64_t stable_fingerprint(const Key &key)
    {
        return stable_fingerprint_impl(key);
    }

    template <int W, int H>
    static uint64_t zobrist_value(unsigned pos, unsigned tile)
    {
        static const std::array<std::array<uint64_t, W * H>, W * H> table = build_zobrist_table<W, H>();
        return table[pos][tile];
    }
    double estimate_fp() const
    {
        if (m_bits == 0 || k_hashes == 0) return 1.0;
        /* fp = (bits_set / m)^k  — uses actual fill, not inflated n_inserted */
        double fill = (double)get_bits_set() / (double)m_bits;
        return pow(fill, (double)k_hashes);
    }

    double get_fill_ratio() const
    {
        if (m_bits == 0) return 0.0;
        return (double)get_bits_set() / (double)m_bits;
    }

    double expected_fill_ratio() const
    {
        if (m_bits == 0 || k_hashes == 0) return 0.0;
        return 1.0 - std::exp(-((double)k_hashes * (double)get_n_unique()) / (double)m_bits);
    }

    size_t get_bits_set() const { return bits_set_count; }

    size_t get_n_inserted() const { return n_inserted; }

protected:
    uint8_t *bits;      /* bit array */
    size_t   m_bits;    /* number of bits */
    size_t   k_hashes;  /* number of hash functions */
    size_t   n_inserted; /* number of inserted items (counts duplicates) */
    size_t   bits_set_count; /* number of 1 bits in the filter */
    uint64_t seed;      /* random seed */
    //std::unordered_set<uint64_t> unique_set; /* exact unique state fingerprints */

    static const void* get_data_ptr(const std::array<int, MN_SIZE*MN_SIZE>& key)
    {
        return key.data();
    }

    static size_t get_data_bytes(const std::array<int, MN_SIZE*MN_SIZE>&)
    {
        return sizeof(int) * MN_SIZE * MN_SIZE;
    }

    // --- OVERLOAD FOR MNPuzzleState ---
    template <int W, int H>
    static const void* get_data_ptr(const MNPuzzleState<W, H>& key)
    {
        return key.puzzle.data();
    }

    template <int W, int H>
    static size_t get_data_bytes(const MNPuzzleState<W, H>&)
    {
        return sizeof(int) * W * H;
    }

    // --- OVERLOAD FOR PancakePuzzleState ---
    template <int N>
    static const void* get_data_ptr(const PancakePuzzleState<N>& key)
    {
        return key.puzzle;
    }

    template <int N>
    static size_t get_data_bytes(const PancakePuzzleState<N>&)
    {
        return sizeof(int) * N;
    }

    // Generic fallback for any trivially solvable state (like PancakePuzzleState)
    template <typename T>
    static const void* get_data_ptr(const T& key)
    {
        return &key;
    }

    template <typename T>
    static size_t get_data_bytes(const T&)
    {
        return sizeof(T);
    }


    void hashes_from_fingerprint(uint64_t fingerprint, uint64_t *h1, uint64_t *h2) const
    {
        *h1 = mix64(fingerprint ^ 0xA5A5A5A5A5A5A5A5ULL ^ seed);
        *h2 = mix64(fingerprint ^ 0x5A5A5A5A5A5A5A5AULL ^ seed);
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

    static uint64_t fnv1a_bytes(const uint8_t* data, size_t byteCount, uint64_t seed)
    {
        uint64_t hash = 14695981039346656037ULL ^ seed;
        const uint64_t fnv_prime = 1099511628211ULL;

        for (size_t i = 0; i < byteCount; i++) {
            hash ^= (uint64_t)data[i];
            hash *= fnv_prime;
        }
        return hash;
    }

    static uint64_t mix64(uint64_t x)
    {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return x;
    }

    static uint64_t splitmix64(uint64_t x)
    {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    template <int W, int H>
    static std::array<std::array<uint64_t, W * H>, W * H> build_zobrist_table()
    {
        std::array<std::array<uint64_t, W * H>, W * H> table{};
        uint64_t x = 0x7f4a7c159e3779b9ULL ^ (uint64_t)(W * 131 + H * 977);
        for (unsigned pos = 0; pos < W * H; ++pos) {
            for (unsigned tile = 0; tile < W * H; ++tile) {
                x = splitmix64(x);
                table[pos][tile] = x;
            }
        }
        return table;
    }


    /* FNV-1a 64-bit hash */
    static uint64_t fnv1a_64(const Key &key, uint64_t seed)
    {
        return fnv1a_bytes(
            reinterpret_cast<const uint8_t*>(get_data_ptr(key)),
            get_data_bytes(key),
            seed
        );
    }

    template <typename T>
    static uint64_t stable_fingerprint_impl(const T &key)
    {
        return fnv1a_bytes(
            reinterpret_cast<const uint8_t*>(get_data_ptr(key)),
            get_data_bytes(key),
            0x9e3779b97f4a7c15ULL
        );
    }

    template <int W, int H>
    static uint64_t stable_fingerprint_impl(const MNPuzzleState<W, H> &state)
    {
        uint64_t h = 0;
        for (unsigned pos = 0; pos < W * H; ++pos) {
            h ^= zobrist_value<W, H>(pos, (unsigned)state.puzzle[pos]);
        }
        return h;
    }

};

#endif /* BLOOM_H */
