#include <array>
#include <cstddef>
#include <cmath>

#include "MNPuzzle.h"
#include "RC.h"

template <std::size_t ALT_LEN>
struct Period2Window {
    static_assert(ALT_LEN >= 2);

    std::array<std::size_t, ALT_LEN> buf{};
    std::size_t head = 0;   // oldest element index in buf
    std::size_t count = 0;  // number of valid elements
    std::size_t bad = 0;    // number of period-2 mismatches (x[i] != x[i-2])

    // Access logical index i (0 = oldest)
    std::size_t at(std::size_t i) const {
        return buf[(head + i) % ALT_LEN];
    }

    void push(std::size_t v) {
        if (count < ALT_LEN) {
            // append at logical index count
            buf[(head + count) % ALT_LEN] = v;

            // new element introduces one constraint if it has an element 2 back
            if (count >= 2) {
                if (v != at(count - 2)) bad++;
            }
            count++;
            return;
        }

        // Window is full: we will drop logical index 0 and add new element at logical index ALT_LEN-1.
        // Before shifting head, remove constraints that reference the leaving element.

        // Constraints are of form at(i) == at(i-2) for i in [2..ALT_LEN-1].
        // Leaving element is logical index 0; it participates only as the "i-2" term for i = 2.
        // That constraint is: at(2) == at(0).
        if (at(2) != at(0)) bad--;  // remove its mismatch contribution

        // Advance head (drop oldest)
        head = (head + 1) % ALT_LEN;

        // Now logical indices have shifted left by 1.
        // We need to insert v as the newest at logical index ALT_LEN-1.
        buf[(head + ALT_LEN - 1) % ALT_LEN] = v;

        // Add the new constraint created by the new last element:
        // at(ALT_LEN-1) should equal at(ALT_LEN-3)
        if (v != at(ALT_LEN - 3)) bad++;
    }

    bool last_n_period2() const {
        if (count < ALT_LEN) return false;
        return bad == 0;
    }
};

template <int W, int H>
size_t get_state_size(const MNPuzzleState<W, H>&)
{
    int n = W * H;
    double log_fact = 0.0;
    for(int i = 1; i <= n; i++) {
        log_fact += std::log2(i);
    }
    return static_cast<size_t>(std::ceil(log_fact));
}

template <int N>
size_t get_state_size(const PancakePuzzleState<N>&)
{
    int n = N;
    double log_fact = 0.0;
    for(int i = 1; i <= n; i++) {
        log_fact += std::log2(i);
    }
    return static_cast<size_t>(std::ceil(log_fact));
}

inline size_t get_state_size(const RCState&)
{
    double log_fact = 0.0;
    for(int i = 1; i <= 12; i++) {
        log_fact += std::log2(i);
    }
    for(int i = 1; i <= 8; i++) {
        log_fact += std::log2(i);
    }
    return static_cast<size_t>(std::ceil(log_fact + 12.0 + 8.0 * std::log2(3.0)));
}
