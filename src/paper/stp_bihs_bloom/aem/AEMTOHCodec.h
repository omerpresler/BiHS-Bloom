#pragma once
#include <type_traits>
#include "AEMTable.h"

template<int N> struct AEMStateCodec<TOHState<N>> {
    static_assert(N <= 32, "TOH lossless codec supports at most 32 disks");
    using Stored = typename std::conditional<(N <= 16), uint32_t, uint64_t>::type;
    static Stored Encode(const TOHState<N> &s) {
        uint64_t rank = 0;
        for (int p = 0; p < 4; ++p)
            for (int i = 0; i < s.GetDiskCountOnPeg(p); ++i)
                rank |= uint64_t(p) << (2 * (s.GetDiskOnPeg(p, i) - 1));
        return static_cast<Stored>(rank);
    }
    static bool Equal(Stored rank, const TOHState<N> &s) { return rank == Encode(s); }
};
