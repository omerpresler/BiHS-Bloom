// Included inside the isolated BiHSBloom class. The original snapshot is not
// modified; EnableAEM is opt-in and legacy calls retain their existing path.
struct AEMEvent {
    int bound, depth;
    bool forward;
    size_t typeIndex, typeCount, inserted, bits, capacity, exact = 0;
    double estimate, seconds = 0;
    uint64_t expanded = 0;
    std::string outcome = "started";
};

void EnableAEM(double alpha = 0.5, bool insertionCountControl = false,
               bool exactBatches = false, bool halfBloom = false) {
    if (!std::isfinite(alpha) || alpha <= 0 || alpha > 1 || (!exactBatches && alpha == 1))
        throw std::invalid_argument("AEM alpha must be in (0,1), or (0,1] with exact batches");
    if (halfBloom && !exactBatches)
        throw std::invalid_argument("Half-budget Bloom filters require exact batches");
    const size_t maxFilters = halfBloom ? 2 : 4;
    if (GetMemoryBudgetBytes() <= maxFilters * (sizeof(AEMFilter) + 1))
        throw std::invalid_argument("AEM budget cannot hold its Bloom buffers and headers");
    aemEnabled = true;
    aemAlpha = alpha;
    aemCountControl = insertionCountControl;
    aemExactBatches = exactBatches;
    aemHalfBloom = halfBloom;
}
bool IsAEMEnabled() const { return aemEnabled; }
size_t GetAEMBloomBytes() const {
    const size_t maxFilters = aemHalfBloom ? 2 : 4;
    return aemEnabled ? GetMemoryBudgetBytes() / maxFilters - sizeof(AEMFilter)
                      : GetMemoryBudgetBytes();
}
size_t GetAEMPeakManagedBytes() const { return aemPeakManagedBytes; }
size_t GetAEMAttempts() const { return aemEvents.size(); }
size_t GetAEMOverflows() const {
    return std::count_if(aemEvents.begin(), aemEvents.end(),
        [](const AEMEvent &e) { return e.outcome == "overflow"; });
}
size_t GetAEMBatchFlushes() const { return aemBatchFlushes; }
size_t GetAEMSeedRebuilds() const { return aemSeedRebuilds; }
size_t GetAEMLiveManagedBytes() const { return aemLiveBloomBytes + aemLiveExactBytes; }
void WriteAEMEvents(const std::string &path) const {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot open AEM event output");
    out << "variant,alpha,total_bound,direction,depth,type_index,type_count,insertion_attempts,"
           "bits_set,m_bits,k_hashes,estimated_distinct,exact_capacity,exact_states,outcome,"
           "seconds,nodes_expanded,managed_budget_bytes,peak_managed_bytes\n";
    out << std::setprecision(17);
    for (const auto &e : aemEvents)
        out << (aemCountControl ? "bihs_bloom_aem_control" : "bihs_bloom_aem") << ','
            << aemAlpha << ',' << e.bound << ',' << (e.forward ? "forward" : "backward") << ','
            << e.depth << ',' << e.typeIndex << ',' << e.typeCount << ',' << e.inserted << ','
            << e.bits << ',' << GetAEMBloomBytes() * 8 << ',' << k_hashes << ',' << e.estimate << ','
            << e.capacity << ',' << e.exact << ',' << e.outcome << ',' << e.seconds << ',' << e.expanded << ','
            << GetMemoryBudgetBytes() << ',' << aemPeakManagedBytes << '\n';
}

private:
bool aemEnabled = false, aemCountControl = false;
bool aemExactBatches = false;
bool aemHalfBloom = false;
size_t aemBatchFlushes = 0;
size_t aemSeedRebuilds = 0;
double aemAlpha = 0.5;
size_t aemLiveBloomBytes = 0, aemLiveExactBytes = 0, aemPeakManagedBytes = 0;
std::vector<AEMEvent> aemEvents;
void AEMObserveMemory() {
    const size_t live = GetAEMLiveManagedBytes();
    if (live > GetMemoryBudgetBytes()) throw std::logic_error("AEM managed memory bound exceeded");
    aemPeakManagedBytes = std::max(aemPeakManagedBytes, live);
}
struct AEMFilter : BiHSBloomFilter<state> {
    BiHSBloom &owner;
    size_t bytes;
    AEMFilter(BiHSBloom &owner, size_t bits, size_t k, uint64_t seed)
        : BiHSBloomFilter<state>(bits, k, seed), owner(owner),
          bytes(sizeof(AEMFilter) + (bits + 7) / 8) {
        if (!this->bits) throw std::bad_alloc();
        owner.aemLiveBloomBytes += bytes;
        owner.AEMObserveMemory();
    }
    ~AEMFilter() { owner.aemLiveBloomBytes -= bytes; }
};
struct AEMReservation {
    BiHSBloom &owner;
    size_t bytes;
    AEMReservation(BiHSBloom &owner, size_t bytes) : owner(owner), bytes(bytes) {
        if (bytes > owner.GetMemoryBudgetBytes() - owner.GetAEMLiveManagedBytes())
            throw std::logic_error("AEM exact reservation exceeds managed budget");
        owner.aemLiveExactBytes += bytes;
        owner.AEMObserveMemory();
    }
    ~AEMReservation() { owner.aemLiveExactBytes -= bytes; }
};
struct AEMAttempt { bool resolved = false; std::vector<action> path; };

struct AEMBatchMatch {};

template<class ScanBatch>
struct AEMBatchInsertion {
    AEMTable<state, action> &table;
    ScanBatch &scan;

    void InsertIfAbsent(uint64_t hash, const state &value, const std::vector<action> &path) {
        try {
            table.InsertIfAbsent(hash, value, path);
        } catch (const AEMTableFull &) {
            // Resume at this leaf after scanning the full batch.
            scan();
            table.Clear();
            table.InsertIfAbsent(hash, value, path);
        }
    }
};

public:
AEMAttempt TryAEM(state start, state goal, int forwardDepth, int backwardDepth,
                  BiHSBloomFilter<state> *bf, bool forward, double &failedEstimate,
                  size_t typeIndex = 0, size_t typeCount = 1) {
    using Table = AEMTable<state, action>;
    const int depth = forward ? forwardDepth : backwardDepth;
    const int bound = forwardDepth + backwardDepth;
    const size_t available = GetMemoryBudgetBytes() - GetAEMLiveManagedBytes();
    const size_t capacity = Table::CapacityForBytes(available, depth);
    const double q = bf->get_fill_ratio();
    if (!capacity || q <= 0) return {};
    const double estimate = q >= 1 ? INFINITY
        : -double(GetAEMBloomBytes() * 8) / k_hashes * std::log1p(-q);
    // Preserve the count-based opportunity; occupancy adds an earlier attempt.
    const bool trigger = bf->get_n_inserted() <= capacity ||
                         (!aemCountControl && estimate <= aemAlpha * capacity);
    // Do not repeatedly pay for an overflowing set with unchanged occupancy.
    if (!trigger || (std::isfinite(failedEstimate) && estimate >= failedEstimate * 0.5)) return {};
    aemEvents.push_back({bound, depth, forward, typeIndex, typeCount,
                         bf->get_n_inserted(), bf->get_bits_set(), capacity, 0, estimate});
    AEMEvent &event = aemEvents.back();
    const auto begin = std::chrono::steady_clock::now();
    const uint64_t workBegin = totalNodesExpanded;
    AEMReservation reservation(*this, Table::BytesForCapacity(capacity, depth));
    Table table(capacity, depth);
    AEMAttempt result;
    auto observeTable = [&] {
        event.exact = std::max(event.exact, table.size());
        peakExactStoredStates = std::max(peakExactStoredStates, table.size());
    };
    auto intersect = [&] {
        const uint64_t intersectionBegin = totalNodesExpanded;
        StateWithPath hit = forward
            ? FindFrontierIntersection(goal, start, backwardDepth, bound, table, true, typeIndex, typeCount)
            : FindFrontierIntersection(start, goal, forwardDepth, bound, table, false, typeIndex, typeCount);
        result.path = std::move(hit.second);
        if (forward && !firstBackwardNodeExpanded)
            firstBackwardNodeExpanded = totalNodesExpanded - intersectionBegin;
        if (!forward && !firstForwardNodeExpanded)
            firstForwardNodeExpanded = totalNodesExpanded - intersectionBegin;
    };
    auto flushFullBatch = [&] {
        ++aemBatchFlushes;
        observeTable();
        auto materializationIndices = std::move(activeLevelIndices);
        intersect();
        if (!result.path.empty()) throw AEMBatchMatch{};
        // The nested scan switches the cached heuristic goal and scan context.
        BiHSBloomHelper::store_goal(env, forward ? goal : start, 0);
        BeginLevelScan(LevelPhase::MATERIALIZATION, forward, depth, bound, scanStats.materializationScans);
        activeLevelIndices = std::move(materializationIndices);
    };
    try {
        if (aemExactBatches) {
            AEMBatchInsertion<decltype(flushFullBatch)> batches{table, flushFullBatch};
            if (forward) FillAEMTable(start, goal, depth, bound, bf, batches, typeIndex, typeCount);
            else FillAEMTable(goal, start, depth, bound, bf, batches, typeIndex, typeCount);
        } else {
            if (forward) FillAEMTable(start, goal, depth, bound, bf, table, typeIndex, typeCount);
            else FillAEMTable(goal, start, depth, bound, bf, table, typeIndex, typeCount);
        }
        observeTable();
        if (!iterStats.empty()) {
            if (forward) iterStats.back().materializedForwardStates = table.size();
            else iterStats.back().materializedBackwardStates = table.size();
            iterStats.back().materializedTotalStates = table.size();
        }
        result.resolved = true;
        if (!table.empty()) intersect();
        event.outcome = table.empty() ? "empty" : (result.path.empty() ? "no_intersection" : "solved");
    } catch (const AEMBatchMatch &) {
        result.resolved = true;
        event.outcome = "solved";
    } catch (const AEMTableFull &) {
        observeTable();
        event.outcome = "overflow";
        failedEstimate = std::isfinite(estimate) ? estimate : double(bf->get_n_inserted());
    } catch (const DeadlineExceeded &) {
        observeTable();
        event.outcome = "timeout";
        event.expanded = totalNodesExpanded - workBegin;
        event.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        throw;
    }
    event.expanded = totalNodesExpanded - workBegin;
    event.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    return result;
}
