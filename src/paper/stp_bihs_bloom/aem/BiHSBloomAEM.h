#include <type_traits>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <fstream>
#include <memory>
#include <set>
#include <chrono>

#include <iomanip>
#include "AEMTable.h"
#include "../../fresh_bytes_20261016/Bloom.h"
#include "../../fresh_bytes_20261016/BloomUtil.h"

namespace BiHSBloomHelper {
    template <typename State, typename Action>
    struct StateFingerprint {
        static constexpr bool incremental = false;

        static uint64_t hash(const State& state)
        {
            return BiHSBloomFilter<State>::stable_fingerprint(state);
        }

        static uint64_t apply(uint64_t, const State& state, Action)
        {
            return hash(state);
        }
    };

    template <int W, int H>
    struct StateFingerprint<MNPuzzleState<W, H>, slideDir> {
        static constexpr bool incremental = true;

        static uint64_t zobrist(unsigned pos, unsigned tile)
        {
            return BiHSBloomFilter<MNPuzzleState<W, H>>::template zobrist_value<W * H>(pos, tile);
        }

        static uint64_t hash(const MNPuzzleState<W, H>& state)
        {
            return BiHSBloomFilter<MNPuzzleState<W, H>>::stable_fingerprint(state);
        }

        static uint64_t apply(uint64_t h, const MNPuzzleState<W, H>& state, slideDir action)
        {
            unsigned blank = state.blank;
            unsigned tile_pos = blank;
            switch (action) {
                case kUp: tile_pos = blank - W; break;
                case kDown: tile_pos = blank + W; break;
                case kLeft: tile_pos = blank - 1; break;
                case kRight: tile_pos = blank + 1; break;
                case kNoSlide: return h;
            }

            unsigned tile = (unsigned)state.puzzle[tile_pos];
            h ^= zobrist(blank, 0);
            h ^= zobrist(tile_pos, tile);
            h ^= zobrist(blank, tile);
            h ^= zobrist(tile_pos, 0);
            return h;
        }
    };

    template <int N>
    struct StateFingerprint<PancakePuzzleState<N>, PancakePuzzleAction> {
        static constexpr bool incremental = true;

        static uint64_t zobrist(unsigned pos, unsigned tile)
        {
            return BiHSBloomFilter<PancakePuzzleState<N>>::template zobrist_value<N>(pos, tile);
        }

        static uint64_t hash(const PancakePuzzleState<N>& state)
        {
            return BiHSBloomFilter<PancakePuzzleState<N>>::stable_fingerprint(state);
        }

        static uint64_t apply(uint64_t h, const PancakePuzzleState<N>& state, PancakePuzzleAction action)
        {
            if (action <= 1) {
                return h;
            }

            unsigned upper = 0;
            unsigned lower = std::min<unsigned>(action - 1, N - 1);
            while (upper < lower) {
                unsigned upperTile = static_cast<unsigned>(state.puzzle[upper]);
                unsigned lowerTile = static_cast<unsigned>(state.puzzle[lower]);
                h ^= zobrist(upper, upperTile);
                h ^= zobrist(lower, lowerTile);
                h ^= zobrist(upper, lowerTile);
                h ^= zobrist(lower, upperTile);
                ++upper;
                --lower;
            }
            return h;
        }
    };

    template <>
    struct StateFingerprint<RCState, RCAction> {
        static constexpr bool incremental = false;

        static uint64_t hash(const RCState& state)
        {
            return BiHSBloomFilter<RCState>::stable_fingerprint(state);
        }

        static uint64_t apply(uint64_t, const RCState& state, RCAction)
        {
            return hash(state);
        }
    };

    template <typename Env, typename State, typename Action>
    auto get_actions(Env& env, const State& curr, std::vector<Action>& actions, Action last_action, int)
        -> decltype(env.GetActions(curr, actions, last_action), void())
    {
        env.GetActions(curr, actions, last_action);
    }

    template <typename Env, typename State, typename Action>
    void get_actions(Env& env, const State& curr, std::vector<Action>& actions, Action last_action, double)
    {
        env.GetActions(curr, actions);
        Action inv = last_action;
        env.InvertAction(inv);
        for(auto it = actions.begin(); it != actions.end(); ++it) {
            if (*it == inv) {
                actions.erase(it);
                break;
            }
        }
    }

    template <typename Env, typename State>
    auto store_goal(Env& env, State& goal, int)
        -> decltype(env.StoreGoal(goal), void())
    {
        env.StoreGoal(goal);
    }

    template <typename Env, typename State>
    void store_goal(Env&, State&, double)
    {
    }
}

#define LOOP_LIMIT 200
enum class TerminationCondition {
    MAX_ITERATIONS,
    MIN_ITEMS,
    STABILIZED,
    NOT_TERMINATED
};

template <class state, class action, class environment>
class BiHSBloom {
public:
    struct DeadlineExceeded {};

    void SetTimeLimitSeconds(int seconds) {
        if (seconds < 0) throw std::invalid_argument("BiHS time limit must be nonnegative");
        timeLimitSeconds = seconds;
    }
    bool DidTimeOut() const { return timedOut; }

    BiHSBloom(int size_in_KiB, int k_hashes)
        : size_in_KiB(size_in_KiB),
          k_hashes(k_hashes),
          stab_tail_len(4),
          env() // default-construct environment
    {
        if (size_in_KiB <= 0) {
            throw std::invalid_argument("size_in_KiB must be positive");
        }
        if (k_hashes <= 0) {
            throw std::invalid_argument("k_hashes must be positive");
        }

        this->min_items = int((long long)size_in_KiB * 1024 * 8 / get_state_size(state{}));
        std::cout << "Bloom filter size: " << size_in_KiB << " KiB, k_hashes: " << k_hashes << ", min_items: " << this->min_items << std::endl;
    }

    BiHSBloom(int size_in_KiB, int k_hashes, const environment &searchEnvironment)
        : size_in_KiB(size_in_KiB),
          k_hashes(k_hashes),
          stab_tail_len(4),
          env(searchEnvironment)
    {
        if (size_in_KiB <= 0) {
            throw std::invalid_argument("size_in_KiB must be positive");
        }
        if (k_hashes <= 0) {
            throw std::invalid_argument("k_hashes must be positive");
        }

        this->min_items = int((long long)size_in_KiB * 1024 * 8 / get_state_size(state{}));
        std::cout << "Bloom filter size: " << size_in_KiB << " KiB, k_hashes: "
                  << k_hashes << ", min_items: " << this->min_items << std::endl;
    }

    struct IterationStat {
        int    totalDepth;
        int    iteration;
        size_t nInserted;
        size_t nUnique;
        double estimatedFP;
        size_t bitsSet;
        double fillRatio;
        double expectedFillRatio;
        size_t materializedForwardStates;
        size_t materializedBackwardStates;
        size_t materializedTotalStates;
        bool   isTypeSplit;
        size_t typeIndex;
        size_t typeCount;
        uint64_t boundCycle;
        int forwardDepth;
        int backwardDepth;
        uint64_t cumulativeForwardBloomScans;
        uint64_t cumulativeBackwardBloomScans;
        uint64_t cumulativeTypeSplitScans;
        uint64_t firstForwardWork;
        uint64_t firstBackwardWork;
        int observedNextF;
        bool isForwardScan;
        uint64_t nodesExpandedThisScan;
        uint64_t cumulativeNodesExpanded;
        bool isStreaming;
        size_t streamBatch;
        double qStop;
    };

    struct ScanStats {
        uint64_t boundCycles = 0;
        uint64_t forwardBloomScans = 0;
        uint64_t backwardBloomScans = 0;
        uint64_t typeSplitScans = 0;
        uint64_t materializationScans = 0;
        uint64_t intersectionScans = 0;
        uint64_t streamBatches = 0;

        uint64_t FrontierScans() const { return forwardBloomScans + backwardBloomScans; }
        uint64_t ExtractionScans() const { return materializationScans + intersectionScans; }
        uint64_t TotalScans() const { return FrontierScans() + ExtractionScans(); }
    };

    struct BoundStat {
        uint64_t cycle;
        int totalBound;
        int forwardDepth;
        int backwardDepth;
        uint64_t forwardWork;
        uint64_t backwardWork;
        int nextBound;
        uint64_t cumulativeNodesExpanded;
    };

    enum class LevelPhase {
        FRONTIER,
        STREAMING,
        MATERIALIZATION,
        INTERSECTION
    };

    // One row is one concrete scan at one unit-cost depth. Counterfactual
    // consistency counts are local tests on nodes retained by the active run;
    // they are not an estimate of the complete alternative search tree.
    struct LevelStat {
        uint64_t boundCycle = 0;
        uint64_t scanOrdinal = 0;
        LevelPhase phase = LevelPhase::FRONTIER;
        bool isForward = true;
        int totalBound = 0;
        int targetDepth = 0;
        int depth = 0;
        uint64_t visits = 0;
        uint64_t expanded = 0;
        uint64_t generated = 0;
        uint64_t accepted = 0;
        uint64_t basePruned = 0;
        uint64_t consistencyChecks = 0;
        uint64_t actualConsistencyPruned = 0;
        uint64_t shadowConsistencyWouldPrune = 0;
        uint64_t bloomChecks = 0;
        uint64_t bloomRejected = 0;
        uint64_t frontierInserted = 0;
    };

    uint64_t GetTotalNodesExpanded() const { return totalNodesExpanded; }
    const std::vector<IterationStat>& GetIterStats() const { return iterStats; }
    const ScanStats& GetScanStats() const { return scanStats; }
    const std::vector<BoundStat>& GetBoundStats() const { return boundStats; }
    const std::vector<LevelStat>& GetLevelStats() const { return levelStats; }
    uint64_t GetTotalNodesGenerated() const { return totalNodesGenerated; }
    // Final threshold only. Refinement passes reuse a bucket. When splitting
    // replaces the initial whole-frontier probe, count visited child types,
    // not that probe as an additional partition. Unvisited types do not count.
    uint64_t GetFinalIterationBuckets() const {
        std::set<size_t> types, batches;
        bool observed = false;
        for (const auto &s : iterStats) {
            if (s.boundCycle != scanStats.boundCycles) continue;
            observed = true;
            if (s.isTypeSplit) types.insert(s.typeIndex);
            if (s.isStreaming && s.iteration == 0 && s.nInserted > 0)
                batches.insert(s.streamBatch);
        }
        if (timedOut) {
            if (streamingEnabled) return startedBatchesThisBound;
            if (!startedTypesThisBound.empty()) return startedTypesThisBound.size();
            return scanStartedThisBound ? 1 : 0;
        }
        if (streamingEnabled) return batches.size();
        return types.empty() ? (observed ? 1 : 0) : types.size();
    }
    void EnableStreaming(double qStop = 0.30) {
        if (!(qStop > 0.0 && qStop < 1.0)) {
            throw std::invalid_argument("streaming q_stop must be in (0, 1)");
        }
        streamingEnabled = true;
        streamingLoadLimitEnabled = true;
        streamingQStop = qStop;
    }
    void EnableStreamingWithoutLoadLimit() {
        streamingEnabled = true;
        streamingLoadLimitEnabled = false;
        // A descriptive numeric sentinel for CSV output. Bloom occupancy is
        // bounded by 1.0, so 1.01 can never end a batch early.
        streamingQStop = 1.01;
    }
    bool IsStreamingEnabled() const { return streamingEnabled; }
    bool HasStreamingLoadLimit() const { return streamingLoadLimitEnabled; }
    double GetStreamingQStop() const { return streamingQStop; }
    void SetBloomSeed(uint64_t seed) {
        fixedBloomSeedEnabled = true;
        bloomSeedBase = seed;
    }
    bool HasFixedBloomSeed() const { return fixedBloomSeedEnabled; }
    uint64_t GetBloomSeed() const { return bloomSeedBase; }
    void SetUseConsistentErrorBound(bool enabled) {
        useConsistentErrorBound = enabled;
    }
    bool UsesConsistentErrorBound() const { return useConsistentErrorBound; }

    // Opt-in byte granularity. Existing KiB constructors remain reproducible.
    // A logical frontier budget excludes paths, hash containers and stacks.
    void SetMemoryBudgetBytes(size_t bytes) {
        const size_t stateBits = get_state_size(state{});
        if (bytes > std::numeric_limits<size_t>::max() / 8 || stateBits == 0)
            throw std::invalid_argument("Invalid byte memory budget");
        const size_t capacity = bytes * 8 / stateBits;
        if (capacity == 0 || capacity > static_cast<size_t>(std::numeric_limits<int>::max()))
            throw std::invalid_argument("Byte budget must hold 1..INT_MAX logical states");
        memoryBudgetBytes = bytes;
        min_items = static_cast<int>(capacity);
    }
    size_t GetMemoryBudgetBytes() const {
        return memoryBudgetBytes ? memoryBudgetBytes : static_cast<size_t>(size_in_KiB) * 1024;
    }
    size_t GetExactStateCapacity() const { return static_cast<size_t>(min_items); }

    void InitBloom(BiHSBloomFilter<state> *&bf) {
        size_t m_bits = GetAEMBloomBytes() * 8;
        if (aemEnabled) {
            const size_t bytes = sizeof(AEMFilter) + GetAEMBloomBytes();
            if (bytes > GetMemoryBudgetBytes() - GetAEMLiveManagedBytes())
                throw std::logic_error("AEM Bloom allocation exceeds managed budget");
            const uint64_t seed = bloomSeedBase + 0x9E3779B97F4A7C15ULL * bloomFilterOrdinal++;
            bf = new AEMFilter(*this, m_bits, k_hashes, seed);
            return;
        }
        if (fixedBloomSeedEnabled) {
            const uint64_t filterSeed = bloomSeedBase +
                0x9E3779B97F4A7C15ULL * bloomFilterOrdinal++;
            bf = new BiHSBloomFilter<state>(m_bits, k_hashes, filterSeed);
        } else {
            bf = new BiHSBloomFilter<state>(m_bits, k_hashes);
        }
    }

    void SanityCheck(state &start, state &goal, std::vector<action> &path) {
        for (auto a : path) {
            env.ApplyAction(start, a);
        }
        if (start != goal) {
            throw std::runtime_error("Path is not valid");
        }
    }

    using StateWithPath = std::pair<state, std::vector<action>>;
    size_t GetPeakExactStoredStates() const { return peakExactStoredStates; }

    // Fingerprints select a small candidate bucket; full state equality decides
    // identity.  Keep every distinct state that shares a fingerprint so an
    // ordinary hash collision cannot create either a false intersection or a
    // false negative.
    class ExactStateTable {
    public:
        explicit ExactStateTable(size_t *peak = nullptr) : peak(peak) {}
        void reserve(size_t count) { entries.reserve(count); }
		size_t size() const { return entries.size(); }
		bool empty() const { return entries.empty(); }

        bool InsertIfAbsent(uint64_t fingerprint, const state &value,
                            const std::vector<action> &path) {
            auto range = entries.equal_range(fingerprint);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second.first == value) return false;
            }
            entries.emplace(fingerprint, StateWithPath{value, path});
            if (peak) *peak = std::max(*peak, entries.size());
            return true;
        }

        const StateWithPath *Find(uint64_t fingerprint, const state &value) const {
            auto range = entries.equal_range(fingerprint);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second.first == value) return &it->second;
            }
            return nullptr;
        }

    private:
        size_t *peak;
        std::unordered_multimap<uint64_t, StateWithPath> entries;
    };

    struct PathExtractionResult {
        std::vector<action> path;
        size_t forwardStates;
        size_t backwardStates;
    };

    bool HashMatchesType(uint64_t hash, size_t typeIndex, size_t typeCount) const {
        return typeCount <= 1 || (hash % typeCount) == typeIndex;
    }

    // Match the conventional search definition used by the baselines: a node
    // is expanded exactly once when its successors are generated.  Keep the
    // per-scan counter for workload splitting and the run-wide counter for
    // reporting in sync at the expansion site.
    void BeginLevelScan(LevelPhase phase, bool isForward, int targetDepth,
                        int totalBound, uint64_t scanOrdinal) {
        if (phase == LevelPhase::FRONTIER || phase == LevelPhase::STREAMING)
            scanStartedThisBound = true;
        activeLevelPhase = phase;
        activeLevelForward = isForward;
        activeLevelTargetDepth = targetDepth;
        activeLevelTotalBound = totalBound;
        activeLevelScanOrdinal = scanOrdinal;
        activeLevelContext = true;
        activeLevelIndices.clear();
    }

    LevelStat &GetLevelStat(int depth) {
        if (depth < 0) {
            throw std::runtime_error("BiHS-Bloom instrumentation received a negative depth");
        }
        const size_t missing = std::numeric_limits<size_t>::max();
        if (activeLevelIndices.size() <= static_cast<size_t>(depth))
            activeLevelIndices.resize(static_cast<size_t>(depth) + 1, missing);
        if (activeLevelIndices[depth] != missing)
            return levelStats[activeLevelIndices[depth]];

        levelStats.push_back(LevelStat{});
        activeLevelIndices[depth] = levelStats.size() - 1;
        LevelStat &result = levelStats.back();
        result.boundCycle = scanStats.boundCycles;
        result.scanOrdinal = activeLevelScanOrdinal;
        result.phase = activeLevelPhase;
        result.isForward = activeLevelForward;
        result.totalBound = activeLevelTotalBound;
        result.targetDepth = activeLevelTargetDepth;
        result.depth = depth;
        return result;
    }

    void RecordVisit(int depth) {
        CheckDeadline();
        if (activeLevelContext) GetLevelStat(depth).visits++;
    }

    void CheckDeadline(bool force = false) {
        if (timeLimitSeconds == 0) return;
        if (!force && (++deadlineVisits & 4095ULL) != 0) return;
        if (std::chrono::steady_clock::now() >= deadline) {
            timedOut = true;
            throw DeadlineExceeded{};
        }
    }

    void RecordNodeExpansion(int depth, size_t generated) {
        this->nodeExpanded++;
        this->totalNodesExpanded++;
        this->totalNodesGenerated += generated;
        if (activeLevelContext) {
            LevelStat &level = GetLevelStat(depth);
            level.expanded++;
            level.generated += generated;
        }
    }

    void RecordAccepted(int depth) {
        if (activeLevelContext) GetLevelStat(depth).accepted++;
    }

    void AddFrontierState(BiHSBloomFilter<state> *bf, uint64_t hash,
                          const state &frontierState, const state &origin,
                          double g, environment *originHeuristicEnv = nullptr) {
        if (originHeuristicEnv) {
            // The main environment has the opposite search endpoint stored as
            // its goal. Some environments (notably MNPuzzle) therefore ignore
            // HCost's second argument. Use a separate environment whose stored
            // goal is the frontier's own origin when computing g-h(x, origin).
            if (g == 0 && frontierState == origin) {
                bf->add_hash(hash, 0.0);
            } else {
                if (!originHeuristicEnv) {
                    throw std::runtime_error(
                        "Consistent-error frontier insertion requires an origin heuristic");
                }
                bf->add_hash(hash, g - originHeuristicEnv->HCost(frontierState, origin));
            }
        } else {
            // g=0 frontier roots have an exact zero error. Other callers must
            // supply an origin environment so shadow consistency remains exact.
            bf->add_hash(hash, 0.0);
        }
    }

    bool PruneByConsistentErrorBound(const state &curr, const state &storedOrigin,
                                     double g, int upperBound,
                                     const BiHSBloomFilter<state> *storedBf) {
        if (!storedBf || !storedBf->has_min_error()) {
            return false;
        }
        if (activeLevelContext) GetLevelStat(static_cast<int>(std::llround(g))).consistencyChecks++;
        const double lowerBound = g + env.HCost(curr, storedOrigin) +
                                  storedBf->get_min_error();
        if (!fgreater(lowerBound, static_cast<double>(upperBound))) {
            return false;
        }
        LevelStat *level = activeLevelContext
            ? &GetLevelStat(static_cast<int>(std::llround(g))) : nullptr;
        if (useConsistentErrorBound) {
            if (level) level->actualConsistencyPruned++;
            UpdateNextSearchBound(static_cast<int>(std::ceil(lowerBound)));
            return true;
        }
        if (level) level->shadowConsistencyWouldPrune++;
        return false;
    }

    template<class Table>
    void GetStatesFromBloomRecursive(state &curr, state &goal, uint64_t currHash, int depth,
                                     int targetDepth, int upperBound, BiHSBloomFilter<state>* bf,
                                     std::vector<action> &movesSoFar,
                                     std::vector<action> &actionScratch,
                                     Table &states,
                                     action lastMove, size_t typeIndex = 0, size_t typeCount = 1) {

        RecordVisit(depth);
        int f_value = static_cast<int>(std::ceil(env.HCost(curr, goal))) + depth;
        if (f_value > upperBound) {
            GetLevelStat(depth).basePruned++;
            UpdateNextSearchBound(f_value);
            return;
        }

        if (depth == targetDepth) {
            LevelStat &level = GetLevelStat(depth);
            level.bloomChecks++;
            if (HashMatchesType(currHash, typeIndex, typeCount) && bf && bf->maybe_contains_hash(currHash)) {
                states.InsertIfAbsent(currHash, curr, movesSoFar);
            } else {
                level.bloomRejected++;
            }
            return;
        }

        actionScratch.clear();
        BiHSBloomHelper::get_actions(env, curr, actionScratch, lastMove, 0);
        std::vector<action> moves = actionScratch;
        RecordNodeExpansion(depth, moves.size());

        for (const action &a : moves) {
            uint64_t nextHash = currHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(currHash, curr, a);
            }
            env.ApplyAction(curr, a);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(curr);
            }
            movesSoFar.push_back(a);
            RecordAccepted(depth);

            GetStatesFromBloomRecursive(curr, goal, nextHash, depth + 1, targetDepth,
                                        upperBound, bf, movesSoFar, actionScratch,
                                        states, a, typeIndex, typeCount);

            // restore
            action inv = a;
            env.InvertAction(inv);
            env.ApplyAction(curr, inv);
            movesSoFar.pop_back();
        }
    }

    ExactStateTable GetStatesFromBloom(state &start, state &goal,
                                                int targetDepth, int upperBound,
                                                BiHSBloomFilter<state>* bf,
                                                size_t typeIndex = 0, size_t typeCount = 1)
    {
        scanStats.materializationScans++;
        BeginLevelScan(LevelPhase::MATERIALIZATION, start == searchStart,
                       targetDepth, upperBound, scanStats.materializationScans);
        BiHSBloomHelper::store_goal(env, goal, 0);

        uint64_t startHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        ExactStateTable states(&peakExactStoredStates);
        states.reserve(bf->get_n_inserted());
        RecordVisit(0);
        if (targetDepth == 0) {
            LevelStat &level = GetLevelStat(0);
            level.bloomChecks++;
            if (HashMatchesType(startHash, typeIndex, typeCount) && bf && bf->maybe_contains_hash(startHash) && env.HCost(start, goal) <= upperBound) {
                states.InsertIfAbsent(startHash, start, {});
            } else {
                level.bloomRejected++;
            }
            return states;
        }
        std::vector<action> moves;
        std::vector<action> actionScratch;
        std::vector<action> movesSoFar;
        movesSoFar.reserve(targetDepth);

        env.GetActions(start, moves);
        RecordNodeExpansion(0, moves.size());

        for (const action &a : moves) {
            uint64_t nextHash = startHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(startHash, start, a);
            }
            env.ApplyAction(start, a);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
            }
            movesSoFar.push_back(a);
            RecordAccepted(0);

            GetStatesFromBloomRecursive(start, goal, nextHash, 1, targetDepth,
                                        upperBound, bf, movesSoFar, actionScratch,
                                        states, a, typeIndex, typeCount);

            action inv = a;
            env.InvertAction(inv);
            env.ApplyAction(start, inv);
            movesSoFar.pop_back();
        }

        return states;
    }

    template<class Table>
    void FillAEMTable(state start, state goal,
                                                int targetDepth, int upperBound,
                                                BiHSBloomFilter<state>* bf,
                                                Table &states,
                                                size_t typeIndex = 0, size_t typeCount = 1)
    {
        scanStats.materializationScans++;
        BeginLevelScan(LevelPhase::MATERIALIZATION, start == searchStart,
                       targetDepth, upperBound, scanStats.materializationScans);
        BiHSBloomHelper::store_goal(env, goal, 0);

        uint64_t startHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        // Fixed table storage was reserved before entering this traversal.
        RecordVisit(0);
        if (targetDepth == 0) {
            LevelStat &level = GetLevelStat(0);
            level.bloomChecks++;
            if (HashMatchesType(startHash, typeIndex, typeCount) && bf && bf->maybe_contains_hash(startHash) && env.HCost(start, goal) <= upperBound) {
                states.InsertIfAbsent(startHash, start, {});
            } else {
                level.bloomRejected++;
            }
            return;
        }
        std::vector<action> moves;
        std::vector<action> actionScratch;
        std::vector<action> movesSoFar;
        movesSoFar.reserve(targetDepth);

        env.GetActions(start, moves);
        RecordNodeExpansion(0, moves.size());

        for (const action &a : moves) {
            uint64_t nextHash = startHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(startHash, start, a);
            }
            env.ApplyAction(start, a);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
            }
            movesSoFar.push_back(a);
            RecordAccepted(0);

            GetStatesFromBloomRecursive(start, goal, nextHash, 1, targetDepth,
                                        upperBound, bf, movesSoFar, actionScratch,
                                        states, a, typeIndex, typeCount);

            action inv = a;
            env.InvertAction(inv);
            env.ApplyAction(start, inv);
            movesSoFar.pop_back();
        }

        return;
    }

    PathExtractionResult GetPathFromBloom(state &start, state &goal,
                                    int forwardDepth, int backwardDepth,
                                    BiHSBloomFilter<state>* bf,
                                    size_t lastForwardInserted,
                                    size_t lastBackwardInserted,
                                    size_t typeIndex = 0, size_t typeCount = 1)
    {
        int upperBound = forwardDepth + backwardDepth;
        bool storeForward = lastForwardInserted <= lastBackwardInserted;
        ExactStateTable statesMap;
        StateWithPath intersection;
        size_t forwardStates = 0;
        size_t backwardStates = 0;

        if (storeForward) {
            statesMap = GetStatesFromBloom(start, goal, forwardDepth, upperBound, bf, typeIndex, typeCount);
            forwardStates = statesMap.size();
            intersection = FindFrontierIntersection(goal, start, backwardDepth,
                                                    upperBound, statesMap, true, typeIndex, typeCount);
            backwardStates = intersection.second.empty() ? 0 : 1;
        } else {
            statesMap = GetStatesFromBloom(goal, start, backwardDepth, upperBound, bf, typeIndex, typeCount);
            backwardStates = statesMap.size();
            intersection = FindFrontierIntersection(start, goal, forwardDepth,
                                                    upperBound, statesMap, false, typeIndex, typeCount);
            forwardStates = intersection.second.empty() ? 0 : 1;
        }

        return {intersection.second, forwardStates, backwardStates};
    }

    PathExtractionResult GetPathFromForwardBloom(state &start, state &goal,
                                    int forwardDepth, int backwardDepth,
                                    BiHSBloomFilter<state>* forwardBf,
                                    size_t typeIndex = 0, size_t typeCount = 1)
    {
        int upperBound = forwardDepth + backwardDepth;
        ExactStateTable statesMap =
            GetStatesFromBloom(start, goal, forwardDepth, upperBound,
                               forwardBf, typeIndex, typeCount);
        size_t forwardStates = statesMap.size();

        if (statesMap.empty()) {
            return {{}, forwardStates, 0};
        }

        StateWithPath intersection = FindFrontierIntersection(goal, start, backwardDepth,
                                                              upperBound, statesMap, true,
                                                              typeIndex, typeCount);
        size_t backwardStates = intersection.second.empty() ? 0 : 1;
        return {intersection.second, forwardStates, backwardStates};
    }

    struct Frame {
        std::vector<action> acts;
        uint64_t hash = 0;
        size_t next = 0;
        action last;
        bool has_last;

        Frame(uint64_t hash) : hash(hash), next(0), has_last(false) {}

        Frame(action lastAction, uint64_t hash) : hash(hash), next(0), last(lastAction), has_last(true) {}
    };

    void LoadFrameActions(Frame &f, const state &curr, std::vector<action> &scratch,
                          int depth) {
        scratch.clear();
        if (f.has_last) {
            BiHSBloomHelper::get_actions(env, curr, scratch, f.last, 0);
        } else {
            env.GetActions(curr, scratch);
        }

        RecordNodeExpansion(depth, scratch.size());

        f.acts = scratch;
        f.next = 0;
    }

    template<class Table>
    StateWithPath FindFrontierIntersection(
        state &start,
        state &goal,
        int targetDepth,
        int upperBound,
        Table &states,
        bool storedStatesAreForward,
        size_t typeIndex = 0,
        size_t typeCount = 1)
    {
        scanStats.intersectionScans++;
        BeginLevelScan(LevelPhase::INTERSECTION, start == searchStart,
                       targetDepth, upperBound, scanStats.intersectionScans);
        BiHSBloomHelper::store_goal(env, goal, 0);

        std::vector<Frame> st;
        st.reserve(targetDepth + 1);
        std::vector<action> movesSoFar;
        movesSoFar.reserve(targetDepth);
        std::vector<action> actionScratch;
        state curr = start;
        uint64_t currHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        st.emplace_back(currHash);

        while (!st.empty()) {
            Frame &f = st.back();
            int depth = (int)st.size() - 1;
            if (f.next == 0 && f.acts.empty()) RecordVisit(depth);

            int f_value = static_cast<int>(std::ceil(env.HCost(curr, goal))) + depth;
            if (f_value > upperBound) {
                GetLevelStat(depth).basePruned++;
                UpdateNextSearchBound(f_value);
                
                // backtrack
                if (st.size() == 1) break;
                    
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                movesSoFar.pop_back();
                currHash = st.back().hash;
                continue;
            }

            // Target depth: check for an exact frontier intersection.
            if (depth == targetDepth) {
                GetLevelStat(depth).bloomChecks++;
                const auto *match = HashMatchesType(currHash, typeIndex, typeCount)
                    ? states.Find(currHash, curr)
                    : nullptr;
                if (match) {
                    const auto &storedPath = match->second;
                    std::vector<action> fullPath;

                    if (storedStatesAreForward) {
                        fullPath.reserve(storedPath.size() + movesSoFar.size());
                        for (const auto &a : storedPath) {
                            fullPath.push_back(a);
                        }
                        for (auto rit = movesSoFar.rbegin(); rit != movesSoFar.rend(); ++rit) {
                            action inv = *rit;
                            env.InvertAction(inv);
                            fullPath.push_back(inv);
                        }
                    } else {
                        fullPath.reserve(movesSoFar.size() + storedPath.size());
                        for (const auto &a : movesSoFar) {
                            fullPath.push_back(a);
                        }
                        for (auto rit = storedPath.rbegin(); rit != storedPath.rend(); ++rit) {
                            action inv = *rit;
                            env.InvertAction(inv);
                            fullPath.push_back(inv);
                        }
                    }

                    return StateWithPath{curr, fullPath};
                }

                GetLevelStat(depth).bloomRejected++;

                if (st.size() == 1) break;
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                movesSoFar.pop_back();
                currHash = st.back().hash;
                continue;
            }

            // If we just arrived to this frame, generate actions once
            if (f.next == 0 && f.acts.empty()) {
                LoadFrameActions(f, curr, actionScratch, depth);
            }

            // If exhausted actions, backtrack
            if (f.next >= f.acts.size()) {
                if (st.size() == 1) break;
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                movesSoFar.pop_back();
                currHash = st.back().hash;
                continue;
            }

            // Otherwise expand next child
            action a = f.acts[f.next++];
            uint64_t nextHash = currHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(currHash, curr, a);
            }
            env.ApplyAction(curr, a);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(curr);
            }
            currHash = nextHash;
            movesSoFar.push_back(a);
            RecordAccepted(depth);
            st.emplace_back(a, currHash); // child frame "remembers" last action
        }

        return StateWithPath{};
    }

    void BuildBloomFrontier(
        state &start,
        state &goal,
        int targetDepth,
        int upperBound,
        BiHSBloomFilter<state>* oldBf,
        BiHSBloomFilter<state>* newBf,
        size_t typeIndex = 0,
        size_t typeCount = 1)
    {
        BiHSBloomHelper::store_goal(env, goal, 0);
        std::unique_ptr<environment> originHeuristicEnv;
        originHeuristicEnv.reset(new environment(env));
        state mutableOrigin = start;
        BiHSBloomHelper::store_goal(*originHeuristicEnv, mutableOrigin, 0);

        std::vector<Frame> st;
        st.reserve(targetDepth + 1);
        std::vector<action> actionScratch;
        state curr = start;
        uint64_t currHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        st.emplace_back(currHash);

        while (!st.empty()) {
            Frame &f = st.back();
            int depth = (int)st.size() - 1;
            if (f.next == 0 && f.acts.empty()) RecordVisit(depth);

            int f_value = static_cast<int>(std::ceil(env.HCost(curr, goal))) + depth;
            if (f_value > upperBound) {
                GetLevelStat(depth).basePruned++;
                UpdateNextSearchBound(f_value);
                
                // backtrack
                if (st.size() == 1) break;
                    
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                currHash = st.back().hash;
                continue;
            }

            if (PruneByConsistentErrorBound(curr, goal, depth, upperBound, oldBf)) {
                if (st.size() == 1) break;

                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                currHash = st.back().hash;
                continue;
            }

            // Target depth: add to Bloom and backtrack (don’t expand deeper)
            if (depth == targetDepth) {
                LevelStat &level = GetLevelStat(depth);
                level.bloomChecks++;
                if (HashMatchesType(currHash, typeIndex, typeCount) &&
                    (!oldBf || oldBf->maybe_contains_hash(currHash))) {
                    AddFrontierState(newBf, currHash, curr, start, depth,
                                     originHeuristicEnv.get());
                    level.frontierInserted++;
                } else {
                    level.bloomRejected++;
                }

                if (st.size() == 1) break;
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                currHash = st.back().hash;
                continue;
            }

            // If we just arrived to this frame, generate actions once
            if (f.next == 0 && f.acts.empty()) {
                LoadFrameActions(f, curr, actionScratch, depth);
            }

            // If exhausted actions, backtrack
            if (f.next >= f.acts.size()) {
                if (st.size() == 1) break;
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                currHash = st.back().hash;
                continue;
            }

            // Otherwise expand next child
            action a = f.acts[f.next++];
            uint64_t nextHash = currHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(currHash, curr, a);
            }
            env.ApplyAction(curr, a);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(curr);
            }
            currHash = nextHash;
            RecordAccepted(depth);
            st.emplace_back(a, currHash); // child frame "remembers" last action
        }
    }

    BiHSBloomFilter<state>* GetBloomOfStatesInBloomAtDepth(state start, state goal, int depth, int upperBound, BiHSBloomFilter<state>* oldBf, size_t typeIndex = 0, size_t typeCount = 1) {

        const bool isForward = start == searchStart;
        BeginLevelScan(LevelPhase::FRONTIER, isForward, depth, upperBound,
                       isForward ? scanStats.forwardBloomScans : scanStats.backwardBloomScans);
        BiHSBloomFilter<state> *rawBf = nullptr;
        InitBloom(rawBf);
        std::unique_ptr<BiHSBloomFilter<state>> bf(rawBf);
        this->nodeExpanded = 0;
        uint64_t startHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);

        if (depth == 0) {
            RecordVisit(0);
            LevelStat &level = GetLevelStat(0);
            level.bloomChecks++;
            if (HashMatchesType(startHash, typeIndex, typeCount) &&
                (!oldBf || oldBf->maybe_contains_hash(startHash))) {
                AddFrontierState(bf.get(), startHash, start, start, 0);
                level.frontierInserted++;
            } else {
                level.bloomRejected++;
            }
            return bf.release();
        }
        
        BuildBloomFrontier(start, goal, depth, upperBound, oldBf, bf.get(), typeIndex, typeCount);

    

        return bf.release();
    }

    // Resume a single depth-first frontier traversal and stop only after the
    // current frontier state has been inserted and the Bloom occupancy reaches
    // q_stop. The saved stack/curr state is the continuation cursor for the
    // next batch, so every feasible frontier state is visited without
    // restarting the forward scan or skipping the state at a batch boundary.
    bool BuildNextStreamingBatch(state &curr, state &goal, const state &origin,
                                 int targetDepth, int upperBound,
                                 std::vector<Frame> &stack,
                                 uint64_t &currHash,
                                 BiHSBloomFilter<state> *bf) {
        BeginLevelScan(LevelPhase::STREAMING, origin == searchStart,
                       targetDepth, upperBound, scanStats.forwardBloomScans);
        BiHSBloomHelper::store_goal(env, goal, 0);
        std::unique_ptr<environment> originHeuristicEnv;
        originHeuristicEnv.reset(new environment(env));
        state mutableOrigin = origin;
        BiHSBloomHelper::store_goal(*originHeuristicEnv, mutableOrigin, 0);
        std::vector<action> actionScratch;

        while (!stack.empty()) {
            Frame &frame = stack.back();
            const int depth = static_cast<int>(stack.size()) - 1;
            if (frame.next == 0 && frame.acts.empty()) RecordVisit(depth);
            const int fValue = static_cast<int>(std::ceil(env.HCost(curr, goal))) + depth;

            if (fValue > upperBound) {
                GetLevelStat(depth).basePruned++;
                UpdateNextSearchBound(fValue);
                if (stack.size() == 1) {
                    stack.clear();
                    break;
                }
                action undo = frame.last;
                stack.pop_back();
                env.UndoAction(curr, undo);
                currHash = stack.back().hash;
                continue;
            }

            if (depth == targetDepth) {
                AddFrontierState(bf, currHash, curr, origin, depth,
                                 originHeuristicEnv.get());
                GetLevelStat(depth).frontierInserted++;
                const bool batchReady = streamingLoadLimitEnabled &&
                                        bf->get_fill_ratio() >= streamingQStop;

                if (stack.size() == 1) {
                    stack.clear();
                } else {
                    action undo = frame.last;
                    stack.pop_back();
                    env.UndoAction(curr, undo);
                    currHash = stack.back().hash;
                }

                if (batchReady) {
                    return stack.empty();
                }
                continue;
            }

            if (frame.next == 0 && frame.acts.empty()) {
                LoadFrameActions(frame, curr, actionScratch, depth);
            }

            if (frame.next >= frame.acts.size()) {
                if (stack.size() == 1) {
                    stack.clear();
                    break;
                }
                action undo = frame.last;
                stack.pop_back();
                env.UndoAction(curr, undo);
                currHash = stack.back().hash;
                continue;
            }

            action nextAction = frame.acts[frame.next++];
            uint64_t nextHash = currHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(
                    currHash, curr, nextAction);
            }
            env.ApplyAction(curr, nextAction);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(curr);
            }
            currHash = nextHash;
            RecordAccepted(depth);
            stack.emplace_back(nextAction, currHash);
        }

        return true;
    }

    size_t ChooseTypeCount(size_t forwardInserted, size_t backwardInserted) const {
        if (forwardInserted == 0 || backwardInserted == 0 || min_items <= 0) {
            return 1;
        }

        double load = std::sqrt(static_cast<double>(forwardInserted) *
                                static_cast<double>(backwardInserted)) /
                      static_cast<double>(min_items);
        if (load <= static_cast<double>(type_split_target_load)) {
            return 1;
        }

        size_t typeCount = static_cast<size_t>(std::ceil(load / static_cast<double>(type_split_target_load)));
        return std::max<size_t>(2, std::min(typeCount, static_cast<size_t>(max_type_splits)));
    }

    std::vector<action> SolveAtDepthForType(state start, state &goal,
                                            int forwardDepth, int backwardDepth,
                                            size_t typeIndex,
                                            size_t typeCount) {
        startedTypesThisBound.insert(typeIndex);
        TerminationCondition term = TerminationCondition::NOT_TERMINATED;
        std::unique_ptr<BiHSBloomFilter<state>> bf;
        std::vector<action> path;
        int upperBound = forwardDepth + backwardDepth;
        size_t lastForwardInserted = 0;
        size_t lastBackwardInserted = 0;
        bool haveForward = false;
        bool haveBackward = false;
        double failedAEM[2] = {INFINITY, INFINITY};

        for (int i = 0; term == TerminationCondition::NOT_TERMINATED; i++) {
            if (i % 2 == 0) {
                scanStats.forwardBloomScans++;
                scanStats.typeSplitScans++;
                bf.reset(GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, upperBound,
                                                        bf.get(), typeIndex, typeCount));
                lastForwardInserted = bf->get_n_inserted();
                haveForward = true;
            } else {
                scanStats.backwardBloomScans++;
                scanStats.typeSplitScans++;
                bf.reset(GetBloomOfStatesInBloomAtDepth(goal, start, backwardDepth, upperBound,
                                                        bf.get(), typeIndex, typeCount));
                lastBackwardInserted = bf->get_n_inserted();
                haveBackward = true;
            }

            iterStats.push_back({
                forwardDepth + backwardDepth,
                i,
                bf->get_n_inserted(),
                bf->get_n_unique(),
                bf->estimate_fp(),
                bf->get_bits_set(),
                bf->get_fill_ratio(),
                bf->expected_fill_ratio(),
                0,
                0,
                0,
                true,
                typeIndex,
                typeCount,
                scanStats.boundCycles,
                forwardDepth,
                backwardDepth,
                scanStats.forwardBloomScans,
                scanStats.backwardBloomScans,
                scanStats.typeSplitScans,
                firstForwardNodeExpanded,
                firstBackwardNodeExpanded,
                nextSearchBound,
                i % 2 == 0,
                nodeExpanded,
                totalNodesExpanded
            });

            if (bf->get_n_inserted() == 0) {
                return {};
            }

            if (aemEnabled) {
                const bool forward = i % 2 == 0;
                auto attempt = TryAEM(start, goal, forwardDepth, backwardDepth, bf.get(), forward,
                                      failedAEM[forward ? 0 : 1], typeIndex, typeCount);
                if (attempt.resolved) return attempt.path;
                
                continue;
            }

            if (haveForward && haveBackward &&
                (lastForwardInserted <= static_cast<size_t>(this->min_items) ||
                 lastBackwardInserted <= static_cast<size_t>(this->min_items))) {
                term = TerminationCondition::MIN_ITEMS;
                break;
            }
        }

        PathExtractionResult extraction = GetPathFromBloom(start, goal, forwardDepth, backwardDepth,
                                                           bf.get(), lastForwardInserted,
                                                           lastBackwardInserted,
                                                           typeIndex, typeCount);
        path = extraction.path;
        if (!iterStats.empty()) {
            iterStats.back().materializedForwardStates = extraction.forwardStates;
            iterStats.back().materializedBackwardStates = extraction.backwardStates;
            iterStats.back().materializedTotalStates = extraction.forwardStates + extraction.backwardStates;
        }
        return path;
    }

    std::vector<action> SolveAtDepthByTypes(state start, state &goal,
                                            int forwardDepth, int backwardDepth,
                                            size_t typeCount) {
        for (size_t typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
            std::vector<action> path = SolveAtDepthForType(start, goal, forwardDepth, backwardDepth,
                                                           typeIndex, typeCount);
            if (!path.empty()) {
                return path;
            }
        }
        return {};
    }

    std::vector<action> SolveAtDepthByTypesFromSeed(state start, state &goal,
                                                    int forwardDepth, int backwardDepth,
                                                    std::unique_ptr<BiHSBloomFilter<state>> seed,
                                                    size_t typeCount) {
        if (!seed) {
            return {};
        }

        int upperBound = forwardDepth + backwardDepth;
        for (size_t typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
            startedTypesThisBound.insert(typeIndex);
            scanStats.forwardBloomScans++;
            scanStats.typeSplitScans++;
            std::unique_ptr<BiHSBloomFilter<state>> typedForward(
                GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, upperBound,
                                               seed.get(), typeIndex, typeCount));

            iterStats.push_back({
                forwardDepth + backwardDepth,
                static_cast<int>(typeIndex),
                typedForward->get_n_inserted(),
                typedForward->get_n_unique(),
                typedForward->estimate_fp(),
                typedForward->get_bits_set(),
                typedForward->get_fill_ratio(),
                typedForward->expected_fill_ratio(),
                0,
                0,
                0,
                true,
                typeIndex,
                typeCount,
                scanStats.boundCycles,
                forwardDepth,
                backwardDepth,
                scanStats.forwardBloomScans,
                scanStats.backwardBloomScans,
                scanStats.typeSplitScans,
                firstForwardNodeExpanded,
                firstBackwardNodeExpanded,
                nextSearchBound,
                true,
                nodeExpanded,
                totalNodesExpanded
            });

            if (typedForward->get_n_inserted() == 0) {
                continue;
            }

            if (aemEnabled) {
                double failed = INFINITY;
                auto attempt = TryAEM(start, goal, forwardDepth, backwardDepth, typedForward.get(),
                                      true, failed, typeIndex, typeCount);
                if (attempt.resolved) {
                    if (!attempt.path.empty()) return attempt.path;
                    continue;
                }
                typedForward.reset();
                auto typedPath = SolveAtDepthForType(start, goal, forwardDepth, backwardDepth,
                                                     typeIndex, typeCount);
                if (!typedPath.empty()) return typedPath;
                continue;
            }

            PathExtractionResult extraction =
                GetPathFromForwardBloom(start, goal, forwardDepth, backwardDepth,
                                        typedForward.get(), typeIndex, typeCount);

            if (!iterStats.empty()) {
                iterStats.back().materializedForwardStates = extraction.forwardStates;
                iterStats.back().materializedBackwardStates = extraction.backwardStates;
                iterStats.back().materializedTotalStates =
                    extraction.forwardStates + extraction.backwardStates;
            }

            if (!extraction.path.empty()) {
                return extraction.path;
            }
        }

        return {};
    }

    std::vector<action> SolveAtDepthStreaming(state start, state &goal,
                                              int forwardDepth, int backwardDepth) {
        const int upperBound = forwardDepth + backwardDepth;
        uint64_t forwardWork = 0;
        uint64_t backwardWork = 0;
        size_t batchId = 0;

        state streamCurr = start;
        uint64_t streamHash =
            BiHSBloomHelper::StateFingerprint<state, action>::hash(streamCurr);
        std::vector<Frame> streamStack;
        streamStack.reserve(forwardDepth + 1);
        streamStack.emplace_back(streamHash);

        auto recordScan = [&](BiHSBloomFilter<state> *bf, int iteration,
                              bool isForward, size_t currentBatch) {
            iterStats.push_back({
                forwardDepth + backwardDepth,
                iteration,
                bf->get_n_inserted(),
                bf->get_n_unique(),
                bf->estimate_fp(),
                bf->get_bits_set(),
                bf->get_fill_ratio(),
                bf->expected_fill_ratio(),
                0,
                0,
                0,
                false,
                0,
                1,
                scanStats.boundCycles,
                forwardDepth,
                backwardDepth,
                scanStats.forwardBloomScans,
                scanStats.backwardBloomScans,
                scanStats.typeSplitScans,
                forwardWork,
                backwardWork,
                nextSearchBound,
                isForward,
                nodeExpanded,
                totalNodesExpanded,
                true,
                currentBatch,
                streamingQStop
            });
        };

        while (!streamStack.empty()) {
            CheckDeadline(true);
            batchId++;
            startedBatchesThisBound++;
            scanStats.streamBatches++;
            scanStats.forwardBloomScans++;

            BiHSBloomFilter<state> *rawBatch = nullptr;
            InitBloom(rawBatch);
            std::unique_ptr<BiHSBloomFilter<state>> bf(rawBatch);
            nodeExpanded = 0;
            const bool streamExhausted =
                BuildNextStreamingBatch(streamCurr, goal, start, forwardDepth, upperBound,
                                        streamStack, streamHash, bf.get());
            const size_t batchForwardInserted = bf->get_n_inserted();
            forwardWork += nodeExpanded;
            recordScan(bf.get(), 0, true, batchId);

            if (batchForwardInserted == 0) {
                if (streamExhausted) {
                    break;
                }
                continue;
            }

            size_t lastForwardInserted = batchForwardInserted;
            size_t lastBackwardInserted = 0;
            bool haveForward = true;
            bool haveBackward = false;
            bool nextIsForward = false;
            bool batchResolved = false;

            for (int iteration = 1; iteration < LOOP_LIMIT; ++iteration) {
                if (nextIsForward) {
                    scanStats.forwardBloomScans++;
                    bf.reset(GetBloomOfStatesInBloomAtDepth(
                        start, goal, forwardDepth, upperBound, bf.get()));
                    lastForwardInserted = bf->get_n_inserted();
                    haveForward = true;
                    forwardWork += nodeExpanded;
                } else {
                    scanStats.backwardBloomScans++;
                    bf.reset(GetBloomOfStatesInBloomAtDepth(
                        goal, start, backwardDepth, upperBound, bf.get()));
                    lastBackwardInserted = bf->get_n_inserted();
                    haveBackward = true;
                    backwardWork += nodeExpanded;
                }

                recordScan(bf.get(), iteration, nextIsForward, batchId);

                if (bf->get_n_inserted() == 0) {
                    batchResolved = true;
                    break;
                }

                if (haveForward && haveBackward &&
                    (lastForwardInserted <= static_cast<size_t>(min_items) ||
                     lastBackwardInserted <= static_cast<size_t>(min_items))) {
                    PathExtractionResult extraction =
                        GetPathFromBloom(start, goal, forwardDepth, backwardDepth,
                                         bf.get(), lastForwardInserted,
                                         lastBackwardInserted);
                    iterStats.back().materializedForwardStates = extraction.forwardStates;
                    iterStats.back().materializedBackwardStates = extraction.backwardStates;
                    iterStats.back().materializedTotalStates =
                        extraction.forwardStates + extraction.backwardStates;
                    if (!extraction.path.empty()) {
                        firstForwardNodeExpanded = forwardWork;
                        firstBackwardNodeExpanded = backwardWork;
                        return extraction.path;
                    }
                    batchResolved = true;
                    break;
                }

                nextIsForward = !nextIsForward;
            }

            if (!batchResolved) {
                throw std::runtime_error(
                    "Streaming refinement exceeded LOOP_LIMIT before resolving a batch");
            }

            if (streamExhausted) {
                break;
            }
        }

        firstForwardNodeExpanded = forwardWork;
        firstBackwardNodeExpanded = backwardWork;
        return {};
    }


    #include "AEMMethods.inc"

    std::vector<action> SolveAtDepth(state start, state &goal, int forwardDepth, int backwardDepth) {
        TerminationCondition term = TerminationCondition::NOT_TERMINATED;
        std::unique_ptr<BiHSBloomFilter<state>> bf;
        std::vector<action> path;
        int upperBound = forwardDepth + backwardDepth; // We know f value from node to goal cant be bigger then Df + Db
        size_t lastForwardInserted = 0;
        size_t lastBackwardInserted = 0;
        bool haveForward = false;
        bool haveBackward = false;
        double failedAEM[2] = {INFINITY, INFINITY};

        this->firstForwardNodeExpanded = 0;
        this->firstBackwardNodeExpanded = 0;

        // Keep iterating until the bloom shrinks to min_items.
        for(int i = 0; term == TerminationCondition::NOT_TERMINATED; i++) {
            if (i % 2 == 0){
                scanStats.forwardBloomScans++;
                bf.reset(GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, upperBound, bf.get()));
                lastForwardInserted = bf->get_n_inserted();
                haveForward = true;
                if(this->firstForwardNodeExpanded == 0)
                    this->firstForwardNodeExpanded = this->nodeExpanded;
            }
            else {
                scanStats.backwardBloomScans++;
                bf.reset(GetBloomOfStatesInBloomAtDepth(goal, start, backwardDepth, upperBound, bf.get()));
                lastBackwardInserted = bf->get_n_inserted();
                haveBackward = true;
                if(this->firstBackwardNodeExpanded == 0)
                    this->firstBackwardNodeExpanded = this->nodeExpanded;
            }

            iterStats.push_back({
                forwardDepth + backwardDepth,
                i,
                bf->get_n_inserted(),
                bf->get_n_unique(),
                bf->estimate_fp(),
                bf->get_bits_set(),
                bf->get_fill_ratio(),
                bf->expected_fill_ratio(),
                0,
                0,
                0,
                false,
                0,
                1,
                scanStats.boundCycles,
                forwardDepth,
                backwardDepth,
                scanStats.forwardBloomScans,
                scanStats.backwardBloomScans,
                scanStats.typeSplitScans,
                firstForwardNodeExpanded,
                firstBackwardNodeExpanded,
                nextSearchBound,
                i % 2 == 0,
                nodeExpanded,
                totalNodesExpanded
            });

            if (bf->get_n_inserted() == 0) {
                return {};
            }

            if (aemEnabled) {
                const bool forward = i % 2 == 0;
                auto attempt = TryAEM(start, goal, forwardDepth, backwardDepth, bf.get(), forward,
                                      failedAEM[forward ? 0 : 1], 0, 1);
                if (attempt.resolved) return attempt.path;
                if (i == 1 && haveForward && haveBackward) {
                    const size_t types = ChooseTypeCount(lastForwardInserted, lastBackwardInserted);
                    if (types > 1) return SolveAtDepthByTypesFromSeed(start, goal, forwardDepth,
                        backwardDepth, std::move(bf), types);
                }
                continue;
            }

            if (haveForward && haveBackward &&
                (lastForwardInserted <= static_cast<size_t>(this->min_items) || 
                lastBackwardInserted <= static_cast<size_t>(this->min_items) )) {
                term = TerminationCondition::MIN_ITEMS;
                break;
            }

            if (i == 1 && haveForward && haveBackward) {
                size_t typeCount = ChooseTypeCount(lastForwardInserted, lastBackwardInserted);
                if (typeCount > 1) {
                    std::cout << "[TYPE] Splitting depth " << (forwardDepth + backwardDepth)
                              << " into " << typeCount << " hash types"
                              << " (Ff=" << lastForwardInserted
                              << ", Fb=" << lastBackwardInserted
                              << ", min_items=" << this->min_items << ")\n" << std::flush;
                    return SolveAtDepthByTypesFromSeed(start, goal, forwardDepth, backwardDepth,
                                                       std::move(bf), typeCount);
                }
            }
        }

        PathExtractionResult extraction = GetPathFromBloom(start, goal, forwardDepth, backwardDepth, bf.get(),
                                                        lastForwardInserted, lastBackwardInserted);
        path = extraction.path;
        if (!iterStats.empty()) {
            iterStats.back().materializedForwardStates = extraction.forwardStates;
            iterStats.back().materializedBackwardStates = extraction.backwardStates;
            iterStats.back().materializedTotalStates = extraction.forwardStates + extraction.backwardStates;
        }

        return path;
    }

    std::vector<action> GetPath(state start, state goal) {
        if (GetAEMLiveManagedBytes()) throw std::logic_error("AEM storage leaked across searches");
        aemEvents.clear();
        aemBatchFlushes = 0;
        aemPeakManagedBytes = 0;
        timedOut = false;
        deadlineVisits = 0;
        scanStartedThisBound = false;
        startedTypesThisBound.clear();
        startedBatchesThisBound = 0;
        if (timeLimitSeconds > 0)
            deadline = std::chrono::steady_clock::now() +
                       std::chrono::seconds(timeLimitSeconds);
        totalNodesExpanded = 0;
        totalNodesGenerated = 0;
        iterStats.clear();
        levelStats.clear();
        activeLevelIndices.clear();
        scanStats = ScanStats{};
        boundStats.clear();
        bloomFilterOrdinal = 0;
        searchStart = start;
        searchGoal = goal;
        activeLevelContext = false;
        BiHSBloomHelper::store_goal(env, goal, 0);
        int fh = env.HCost(start, goal);
        BiHSBloomHelper::store_goal(env, start, 0);
        int bh = env.HCost(goal, start);
        int distance = std::max(fh, bh);

        // Lets test it by letting it stabilized from the start
        //distance = 2 + (distance % 2);

        int forwardDepth = distance / 2;
        int backwardDepth = distance - forwardDepth;

        while(true){
            CheckDeadline(true);
            const int currentBound = forwardDepth + backwardDepth;
            ResetNextSearchBound(currentBound);
            scanStats.boundCycles++;
            scanStartedThisBound = false;
            startedTypesThisBound.clear();
            startedBatchesThisBound = 0;
            boundStats.push_back({scanStats.boundCycles, currentBound, forwardDepth, backwardDepth,
                                  0, 0, -1, 0});

            std::vector<action> path = streamingEnabled
                ? SolveAtDepthStreaming(start, goal, forwardDepth, backwardDepth)
                : SolveAtDepth(start, goal, forwardDepth, backwardDepth);

            BoundStat &boundStat = boundStats.back();
            boundStat.forwardWork = firstForwardNodeExpanded;
            boundStat.backwardWork = firstBackwardNodeExpanded;
            boundStat.cumulativeNodesExpanded = totalNodesExpanded;

            if (path.size() > 0){
                SanityCheck(start, goal, path);
                return path;
            }
            
            int totalDepth = GetNextSearchBound(currentBound);
            boundStat.nextBound = totalDepth;

            if (this->firstForwardNodeExpanded == 0 ||
                this->firstBackwardNodeExpanded == 0) {
                forwardDepth = (totalDepth + 1) / 2;
                backwardDepth = totalDepth - forwardDepth;
            } else {
                forwardDepth = CalculateForwardDepthByWorkload(
                    totalDepth, currentBound, forwardDepth,
                    this->firstForwardNodeExpanded, this->firstBackwardNodeExpanded);
                forwardDepth = std::max(0, std::min(totalDepth, forwardDepth));
                backwardDepth = totalDepth - forwardDepth;
            }

            


        }
    }

private:
    int CalculateForwardDepthByWorkload(int newBound, int previousBound, int oldForwardDepth,
                                        uint64_t forwardLoad, uint64_t backwardLoad) const {
        if (forwardLoad <= backwardLoad) {
            return oldForwardDepth + newBound - previousBound;
        }
        return oldForwardDepth;
    }

    void ResetNextSearchBound(int currentBound) {
        this->currentSearchBound = currentBound;
        this->nextSearchBound = -1;
    }

    void UpdateNextSearchBound(int fValue) {
        if (fValue <= this->currentSearchBound) {
            return;
        }
        if (this->nextSearchBound == -1 || fValue < this->nextSearchBound) {
            this->nextSearchBound = fValue;
        }
    }

    int GetNextSearchBound(int currentBound) const {
        if (this->nextSearchBound > currentBound) {
            return this->nextSearchBound;
        }
        return currentBound + 2;
    }

    size_t memoryBudgetBytes = 0;
    size_t peakExactStoredStates = 0;
    int size_in_KiB;
    int k_hashes;
    int stab_tail_len;
    int min_items;

    uint64_t  firstForwardNodeExpanded = 0;
    uint64_t  firstBackwardNodeExpanded = 0;
    uint64_t  nodeExpanded = 0;
    uint64_t totalNodesExpanded = 0;
    uint64_t totalNodesGenerated = 0;
    std::vector<IterationStat> iterStats;
    std::vector<LevelStat> levelStats;
    std::vector<size_t> activeLevelIndices;

    state searchStart{};
    state searchGoal{};
    bool activeLevelContext = false;
    LevelPhase activeLevelPhase = LevelPhase::FRONTIER;
    bool activeLevelForward = true;
    int activeLevelTargetDepth = 0;
    int activeLevelTotalBound = 0;
    uint64_t activeLevelScanOrdinal = 0;

    int currentSearchBound = -1;
    int nextSearchBound = -1;

    bool streamingEnabled = false;
    bool streamingLoadLimitEnabled = true;
    double streamingQStop = 0.30;
    bool fixedBloomSeedEnabled = false;
    uint64_t bloomSeedBase = 0;
    uint64_t bloomFilterOrdinal = 0;
    bool useConsistentErrorBound = false;
    int type_split_target_load = 25;
    int max_type_splits = 8;

    int timeLimitSeconds = 0;
    bool timedOut = false;
    uint64_t deadlineVisits = 0;
    std::chrono::steady_clock::time_point deadline{};
    bool scanStartedThisBound = false;
    std::set<size_t> startedTypesThisBound;
    size_t startedBatchesThisBound = 0;

    ScanStats scanStats;
    std::vector<BoundStat> boundStats;

    environment env;
};
