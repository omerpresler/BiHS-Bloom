#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

// The default codec retains the full state. Domain specializations must be
// lossless: a fingerprint alone is not an exact state representation.
template<class State> struct AEMStateCodec {
    using Stored = State;
    static Stored Encode(const State &s) { return s; }
    static bool Equal(const Stored &stored, const State &s) { return stored == s; }
};

struct AEMTableFull {};

template<class Action> struct AEMPathView {
    const Action *data = nullptr;
    size_t length = 0;
    size_t size() const { return length; }
    const Action *begin() const { return data; }
    const Action *end() const { return length ? data + length : data; }
    std::reverse_iterator<const Action*> rbegin() const {
        return std::reverse_iterator<const Action*>(end());
    }
    std::reverse_iterator<const Action*> rend() const {
        return std::reverse_iterator<const Action*>(begin());
    }
};

// Two slots per possible entry (load <= 1/2), with one fixed-depth path per
// entry. No insertion allocates memory. Accounted bytes include this object,
// all slots (including unused slots), and the entire path arena.
template<class State, class Action> class AEMTable {
    struct Slot {
        uint64_t fingerprint = 0;
        typename AEMStateCodec<State>::Stored state{};
        uint32_t ordinal = UINT32_MAX;
    };
public:
    using Match = std::pair<State, AEMPathView<Action>>;
    static size_t BytesPerEntry(size_t depth) {
        if (depth > (std::numeric_limits<size_t>::max() - 2 * sizeof(Slot)) / sizeof(Action))
            throw std::invalid_argument("AEM path depth overflows storage calculation");
        return 2 * sizeof(Slot) + depth * sizeof(Action);
    }
    static size_t CapacityForBytes(size_t bytes, size_t depth) {
        if (bytes <= sizeof(AEMTable)) return 0;
        return std::min<size_t>((bytes - sizeof(AEMTable)) / BytesPerEntry(depth), UINT32_MAX - 1);
    }
    static size_t BytesForCapacity(size_t capacity, size_t depth) {
        const size_t perEntry = BytesPerEntry(depth);
        if (capacity > (std::numeric_limits<size_t>::max() - sizeof(AEMTable)) / perEntry)
            throw std::invalid_argument("AEM table capacity overflows storage calculation");
        return sizeof(AEMTable) + capacity * perEntry;
    }
    AEMTable(size_t capacity, size_t depth)
        : capacity_(capacity), depth_(depth), slots_(capacity ? new Slot[2 * capacity] : nullptr),
          paths_(capacity && depth ? new Action[capacity * depth] : nullptr) {
        if (!capacity || capacity >= UINT32_MAX)
            throw std::invalid_argument("AEM table needs a positive representable capacity");
    }
    size_t size() const { return count_; }
    bool empty() const { return !count_; }
    size_t capacity() const { return capacity_; }
    size_t bytes() const { return BytesForCapacity(capacity_, depth_); }
    void Clear() {
        for (size_t i = 0; i < 2 * capacity_; ++i) slots_[i].ordinal = UINT32_MAX;
        count_ = 0;
    }
    bool InsertIfAbsent(uint64_t fingerprint, const State &state,
                        const std::vector<Action> &path) {
        if (path.size() != depth_) throw std::logic_error("AEM path has wrong depth");
        Slot &slot = Probe(fingerprint, state);
        if (slot.ordinal != UINT32_MAX) return false;
        if (count_ == capacity_) throw AEMTableFull{};
        slot.fingerprint = fingerprint;
        slot.state = AEMStateCodec<State>::Encode(state);
        slot.ordinal = static_cast<uint32_t>(count_);
        if (depth_) std::copy(path.begin(), path.end(), paths_.get() + count_ * depth_);
        ++count_;
        return true;
    }
    const Match *Find(uint64_t fingerprint, const State &state) const {
        const Slot &slot = Probe(fingerprint, state);
        if (slot.ordinal == UINT32_MAX) return nullptr;
        match_.first = state;
        match_.second = {depth_ ? paths_.get() + slot.ordinal * depth_ : nullptr, depth_};
        return &match_;
    }
private:
    Slot &Probe(uint64_t fingerprint, const State &state) const {
        size_t i = fingerprint % (2 * capacity_);
        for (size_t visited = 0; visited < 2 * capacity_; ++visited) {
            Slot &slot = slots_[i];
            if (slot.ordinal == UINT32_MAX ||
                (slot.fingerprint == fingerprint && AEMStateCodec<State>::Equal(slot.state, state)))
                return slot;
            if (++i == 2 * capacity_) i = 0;
        }
        throw std::logic_error("AEM half-full table has no empty slot");
    }
    size_t capacity_, depth_, count_ = 0;
    std::unique_ptr<Slot[]> slots_;
    std::unique_ptr<Action[]> paths_;
    mutable Match match_{};
};
