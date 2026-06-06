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

#include "Bloom.h"
#include "BloomUtil.h"
#include "Timer.h"

namespace BiHSBloomHelper {
    template <typename State, typename Action>
    struct StateFingerprint {
        static constexpr bool incremental = false;

        static uint64_t hash(const State& state)
        {
            return BloomFilter<State>::stable_fingerprint(state);
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
            return BloomFilter<MNPuzzleState<W, H>>::template zobrist_value<W, H>(pos, tile);
        }

        static uint64_t hash(const MNPuzzleState<W, H>& state)
        {
            return BloomFilter<MNPuzzleState<W, H>>::stable_fingerprint(state);
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
#define BRANCH_FACTOR 16

enum class TerminationCondition {
    MAX_ITERATIONS,
    MIN_ITEMS,
    STABILIZED,
    NOT_TERMINATED
};

template <class state, class action, class environment>
class BiHSBloom {
public:
    BiHSBloom(int size_in_KiB, int k_hashes, double time_limit_seconds = 0)
        : size_in_KiB(size_in_KiB),
          k_hashes(k_hashes),
          stab_tail_len(4),
          time_limit(time_limit_seconds),
          timed_out(false),
          env() // default-construct environment
    {
        if (size_in_KiB <= 0) {
            throw std::invalid_argument("size_in_KiB must be positive");
        }
        if (k_hashes <= 0) {
            throw std::invalid_argument("k_hashes must be positive");
        }

        this->min_items = int((long long)size_in_KiB * 1024 * 8 / get_state_size(state{}));
        //debug
        std::cout << "Bloom filter size: " << size_in_KiB << " KiB, k_hashes: " << k_hashes << ", min_items: " << this->min_items << std::endl;
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
    };

    bool hasTimedOut() const { return timed_out; }
    uint64_t GetTotalNodesExpanded() const { return totalNodesExpanded; }
    const std::vector<IterationStat>& GetIterStats() const { return iterStats; }

    void InitBloom(BloomFilter<state> *&bf) {
        size_t m_bits = size_in_KiB * 1024 * 8ULL;
        bf = new BloomFilter<state>(m_bits, k_hashes);
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

    struct PathExtractionResult {
        std::vector<action> path;
        size_t forwardStates;
        size_t backwardStates;
    };

    bool HashMatchesType(uint64_t hash, size_t typeIndex, size_t typeCount) const {
        return typeCount <= 1 || (hash % typeCount) == typeIndex;
    }

    void GetStatesFromBloomRecursive(state &curr, state &goal, uint64_t currHash, int depth,
                                     int targetDepth, int upperBound, BloomFilter<state>* bf,
                                     std::vector<action> &movesSoFar,
                                     std::vector<action> &actionScratch,
                                     std::unordered_map<uint64_t, StateWithPath> &states,
                                     action lastMove, size_t typeIndex = 0, size_t typeCount = 1) {

        if (env.HCost(curr, goal) + depth > upperBound) return;

        if (depth == targetDepth) {
            if (HashMatchesType(currHash, typeIndex, typeCount) && bf && bf->maybe_contains_hash(currHash)) {
                if (states.find(currHash) == states.end()) {
                    states.emplace(currHash, StateWithPath{curr, movesSoFar});
                }
            }
            return;
        }

        std::array<action, BRANCH_FACTOR> moves;
        actionScratch.clear();
        BiHSBloomHelper::get_actions(env, curr, actionScratch, lastMove, 0);
        if (actionScratch.size() > moves.size()) {
            throw std::runtime_error("Frame action capacity exceeded");
        }

        uint8_t actCount = static_cast<uint8_t>(actionScratch.size());
        for (uint8_t i = 0; i < actCount; ++i) {
            moves[i] = actionScratch[i];
        }

        for (uint8_t i = 0; i < actCount; ++i) {
            action a = moves[i];
            uint64_t nextHash = currHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(currHash, curr, a);
            }
            env.ApplyAction(curr, a);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(curr);
            }
            movesSoFar.push_back(a);

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

    std::unordered_map<uint64_t, StateWithPath> GetStatesFromBloom(state &start, state &goal,
                                                int targetDepth, int upperBound,
                                                BloomFilter<state>* bf,
                                                size_t typeIndex = 0, size_t typeCount = 1)
    {
        BiHSBloomHelper::store_goal(env, goal, 0);

        uint64_t startHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        std::unordered_map<uint64_t, StateWithPath> states;
        if (targetDepth == 0) {
            if (HashMatchesType(startHash, typeIndex, typeCount) && bf && bf->maybe_contains_hash(startHash) && env.HCost(start, goal) <= upperBound) {
                states.emplace(startHash, StateWithPath{start, {}});
            }
            return states;
        }
        std::vector<action> moves;
        std::vector<action> actionScratch;
        std::vector<action> movesSoFar;
        actionScratch.reserve(BRANCH_FACTOR);
        movesSoFar.reserve(targetDepth);

        env.GetActions(start, moves);

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

    PathExtractionResult GetPathFromBloom(state &start, state &goal,
                                    int forwardDepth, int backwardDepth,
                                    BloomFilter<state>* bf,
                                    size_t lastForwardInserted,
                                    size_t lastBackwardInserted,
                                    size_t typeIndex = 0, size_t typeCount = 1)
    {
        int upperBound = forwardDepth + backwardDepth;
        bool storeForward = lastForwardInserted <= lastBackwardInserted;
        std::unordered_map<uint64_t, StateWithPath> statesMap;
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
                                    BloomFilter<state>* forwardBf,
                                    size_t typeIndex = 0, size_t typeCount = 1)
    {
        int upperBound = forwardDepth + backwardDepth;
        std::unordered_map<uint64_t, StateWithPath> statesMap =
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
        std::array<action, BRANCH_FACTOR> acts;
        uint64_t hash = 0;
        uint8_t actCount = 0;
        uint8_t next = 0;
        action last;
        bool has_last;

        Frame(uint64_t hash) : hash(hash), actCount(0), next(0), has_last(false) {}

        Frame(action lastAction, uint64_t hash) : hash(hash), actCount(0), next(0), last(lastAction), has_last(true) {}
    };

    void LoadFrameActions(Frame &f, const state &curr, std::vector<action> &scratch) {
        scratch.clear();
        if (f.has_last) {
            BiHSBloomHelper::get_actions(env, curr, scratch, f.last, 0);
        } else {
            env.GetActions(curr, scratch);
        }

        if (scratch.size() > f.acts.size()) {
            throw std::runtime_error("Frame action capacity exceeded");
        }

        f.actCount = static_cast<uint8_t>(scratch.size());
        for (uint8_t i = 0; i < f.actCount; ++i) {
            f.acts[i] = scratch[i];
        }
    }

    StateWithPath FindFrontierIntersection(
        state &start,
        state &goal,
        int targetDepth,
        int upperBound,
        std::unordered_map<uint64_t, StateWithPath> &states,
        bool storedStatesAreForward,
        size_t typeIndex = 0,
        size_t typeCount = 1)
    {
        BiHSBloomHelper::store_goal(env, goal, 0);

        std::vector<Frame> st;
        st.reserve(targetDepth + 1);
        std::vector<action> movesSoFar;
        movesSoFar.reserve(targetDepth);
        std::vector<action> actionScratch;
        actionScratch.reserve(BRANCH_FACTOR);
        state curr = start;
        uint64_t currHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        st.emplace_back(currHash);

        while (!st.empty()) {
            this->nodeExpanded++;
            Frame &f = st.back();
            int depth = (int)st.size() - 1;

            int f_value = static_cast<int>(std::ceil(env.HCost(curr, goal))) + depth;
            if (f_value > upperBound) {
                if (this->min_f_value == -1 || this->min_f_value > f_value){
                    // std::cout << "New Min F Value: " << f_value << std::endl;
                    this->min_f_value = f_value;
                }
                
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
                auto it = HashMatchesType(currHash, typeIndex, typeCount)
                            ? states.find(currHash)
                            : states.end();
                if (it != states.end()) {
                    const std::vector<action> &storedPath = it->second.second;
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

                if (st.size() == 1) break;
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                movesSoFar.pop_back();
                currHash = st.back().hash;
                continue;
            }

            // If we just arrived to this frame, generate actions once
            if (f.next == 0 && f.actCount == 0) {
                LoadFrameActions(f, curr, actionScratch);
            }

            // If exhausted actions, backtrack
            if (f.next >= f.actCount) {
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
            st.emplace_back(a, currHash); // child frame "remembers" last action
        }

        return StateWithPath{};
    }

    void BuildBloomFrontier(
        state &start,
        state &goal,
        int targetDepth,
        int upperBound,
        BloomFilter<state>* oldBf,
        BloomFilter<state>* newBf,
        size_t typeIndex = 0,
        size_t typeCount = 1)
    {
        BiHSBloomHelper::store_goal(env, goal, 0);

        std::vector<Frame> st;
        st.reserve(targetDepth + 1);
        std::vector<action> actionScratch;
        actionScratch.reserve(BRANCH_FACTOR);
        state curr = start;
        uint64_t currHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        st.emplace_back(currHash);

        while (!st.empty()) {
            this->nodeExpanded++;
            Frame &f = st.back();
            int depth = (int)st.size() - 1;

            int f_value = static_cast<int>(std::ceil(env.HCost(curr, goal))) + depth;
            if (f_value > upperBound) {
                if (this->min_f_value == -1 || this->min_f_value > f_value){
                    // std::cout << "New Min F Value: " << f_value << std::endl;
                    this->min_f_value = f_value;
                }
                
                // backtrack
                if (st.size() == 1) break;
                    
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                currHash = st.back().hash;
                continue;
            }

            // Target depth: add to Bloom and backtrack (don’t expand deeper)
            if (depth == targetDepth) {
                if (HashMatchesType(currHash, typeIndex, typeCount) &&
                    (!oldBf || oldBf->maybe_contains_hash(currHash)))
                    newBf->add_hash(currHash);

                if (st.size() == 1) break;
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                currHash = st.back().hash;
                continue;
            }

            // If we just arrived to this frame, generate actions once
            if (f.next == 0 && f.actCount == 0) {
                LoadFrameActions(f, curr, actionScratch);
            }

            // If exhausted actions, backtrack
            if (f.next >= f.actCount) {
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
            st.emplace_back(a, currHash); // child frame "remembers" last action
        }
    }

    BloomFilter<state>* GetBloomOfStatesInBloomAtDepth(state start, state goal, int depth, int upperBound, BloomFilter<state>* oldBf, size_t typeIndex = 0, size_t typeCount = 1) {

        BloomFilter<state> *bf = nullptr;
        InitBloom(bf);
        this->nodeExpanded = 0;
        uint64_t startHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);

        if (depth == 0) {
            this->nodeExpanded++;
            if (HashMatchesType(startHash, typeIndex, typeCount) &&
                (!oldBf || oldBf->maybe_contains_hash(startHash))) {
                bf->add_hash(startHash);
            }
            this->totalNodesExpanded += this->nodeExpanded;
            return bf;
        }
        
        BuildBloomFrontier(start, goal, depth, upperBound, oldBf, bf, typeIndex, typeCount);

    

        this->totalNodesExpanded += this->nodeExpanded;
        return bf;
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
                                            Timer &globalTimer,
                                            size_t typeIndex,
                                            size_t typeCount) {
        TerminationCondition term = TerminationCondition::NOT_TERMINATED;
        std::unique_ptr<BloomFilter<state>> bf;
        std::vector<action> path;
        int upperBound = forwardDepth + backwardDepth;
        size_t lastForwardInserted = 0;
        size_t lastBackwardInserted = 0;
        bool haveForward = false;
        bool haveBackward = false;

        for (int i = 0; term == TerminationCondition::NOT_TERMINATED; i++) {
            if (time_limit > 0) {
                globalTimer.EndTimer();
                if (globalTimer.GetElapsedTime() > time_limit) {
                    timed_out = true;
                    return {};
                }
            }

            if (i % 2 == 0) {
                bf.reset(GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, upperBound,
                                                        bf.get(), typeIndex, typeCount));
                lastForwardInserted = bf->get_n_inserted();
                haveForward = true;
            } else {
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
                typeCount
            });

            if (bf->get_n_inserted() == 0) {
                return {};
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
                                            Timer &globalTimer,
                                            size_t typeCount) {
        for (size_t typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
            std::vector<action> path = SolveAtDepthForType(start, goal, forwardDepth, backwardDepth,
                                                           globalTimer, typeIndex, typeCount);
            if (timed_out || !path.empty()) {
                return path;
            }
        }
        return {};
    }

    std::vector<action> SolveAtDepthByTypesFromSeed(state start, state &goal,
                                                    int forwardDepth, int backwardDepth,
                                                    Timer &globalTimer,
                                                    std::unique_ptr<BloomFilter<state>> seed,
                                                    size_t typeCount) {
        if (!seed) {
            return {};
        }

        int upperBound = forwardDepth + backwardDepth;
        for (size_t typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
            if (time_limit > 0) {
                globalTimer.EndTimer();
                if (globalTimer.GetElapsedTime() > time_limit) {
                    timed_out = true;
                    return {};
                }
            }

            std::unique_ptr<BloomFilter<state>> typedForward(
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
                typeCount
            });

            if (typedForward->get_n_inserted() == 0) {
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


    std::vector<action> SolveAtDepth(state start, state &goal, int forwardDepth, int backwardDepth , Timer &globalTimer) {
        TerminationCondition term = TerminationCondition::NOT_TERMINATED;
        std::unique_ptr<BloomFilter<state>> bf;
        std::vector<action> path;
        int upperBound = forwardDepth + backwardDepth; // We know f value from node to goal cant be bigger then Df + Db
        size_t lastForwardInserted = 0;
        size_t lastBackwardInserted = 0;
        bool haveForward = false;
        bool haveBackward = false;

        this->firstForwardNodeExpanded = 0;
        this->firstBackwardNodeExpanded = 0;

        //std::cout << "[PROF] Starting SolveAtDepth fd=" << forwardDepth << " bd=" << backwardDepth << " ub=" << upperBound << std::endl;

        // Keep iterating until we either time out or shrink the bloom to min_items.
        for(int i = 0; term == TerminationCondition::NOT_TERMINATED; i++) {
            // Check time limit
            if (time_limit > 0) {
                globalTimer.EndTimer();
                if (globalTimer.GetElapsedTime() > time_limit) {
                    timed_out = true;
                    return {};
                }
            }

            Timer iterTimer;
            iterTimer.StartTimer();

            if (i % 2 == 0){
                bf.reset(GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, upperBound, bf.get()));
                lastForwardInserted = bf->get_n_inserted();
                haveForward = true;
                if(this->firstForwardNodeExpanded == 0)
                    this->firstForwardNodeExpanded = this->nodeExpanded;
            }
            else {
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
                1
            });

            
            iterTimer.EndTimer();
            {
                std::ofstream proof_log("proof_unique.csv", std::ios::app);
                proof_log << (forwardDepth + backwardDepth) << "," << i << ","
                          << bf->get_n_inserted() << "," << bf->get_n_unique() << ","
                          << bf->get_bits_set() << "," << bf->get_fill_ratio() << ","
                          << bf->expected_fill_ratio() << "\n";
            }

            if (bf->get_n_inserted() == 0) {
                return {};
            }

            if (haveForward && haveBackward &&
                (lastForwardInserted <= static_cast<size_t>(this->min_items) || 
                lastBackwardInserted <= static_cast<size_t>(this->min_items) )) {
                term = TerminationCondition::MIN_ITEMS;
                //std::cout << "[PROF] Min items reached at iter " << i << std::endl;
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
                                                       globalTimer, std::move(bf), typeCount);
                }
            }
        }

        //std::cout << "[PROF] BuildBloom loop finished. Starting GetPathFromBloom." << std::endl;

        Timer pathTimer;
        pathTimer.StartTimer();
        PathExtractionResult extraction = GetPathFromBloom(start, goal, forwardDepth, backwardDepth, bf.get(),
                                                        lastForwardInserted, lastBackwardInserted);
        path = extraction.path;
        if (!iterStats.empty()) {
            iterStats.back().materializedForwardStates = extraction.forwardStates;
            iterStats.back().materializedBackwardStates = extraction.backwardStates;
            iterStats.back().materializedTotalStates = extraction.forwardStates + extraction.backwardStates;
        }
        pathTimer.EndTimer();
        //std::cout << "[PROF] GetPathFromBloom time=" << pathTimer.GetElapsedTime() << "s" << std::endl;

        return path;
    }

    std::vector<action> GetPath(state start, state goal) {
        timed_out = false;
        totalNodesExpanded = 0;
        iterStats.clear();
        BiHSBloomHelper::store_goal(env, goal, 0);
        int fh = env.HCost(start, goal);
        BiHSBloomHelper::store_goal(env, start, 0);
        int bh = env.HCost(goal, start);
        int distance = std::max(fh, bh);

        // Lets test it by letting it stabilized from the start
        //distance = 2 + (distance % 2);

        int forwardDepth = distance / 2;
        int backwardDepth = distance - forwardDepth;
        Timer globalTimer;
        globalTimer.StartTimer();
        bool hasLearnedSplit = false;
        bool dynamicSplitting = false;

        while(true){
            // Check time limit at each depth iteration
            if (time_limit > 0) {
                globalTimer.EndTimer();
                if (globalTimer.GetElapsedTime() > time_limit) {
                    timed_out = true;
                    return {};
                }
            }

            //std::cout << "[PROF] Trying depth: " << forwardDepth << " + " << backwardDepth << " = " << (forwardDepth + backwardDepth) << std::endl;
            std::vector<action> path = SolveAtDepth(start, goal, forwardDepth, backwardDepth, globalTimer);

            if (timed_out) return {};

            if (path.size() > 0){
                //SanityCheck(start, goal, path);
                //totalTimer.EndTimer();
                //std::cout << "[PROF] Total GetPath time=" << totalTimer.GetElapsedTime() << "s" << std::endl;
                return path;
            }
            
            //std::cout << "Forward Node Expanded: " << this->firstForwardNodeExpanded << " Backward Node Expanded: " << this->firstBackwardNodeExpanded << std::endl;

            int totalDepth = this->min_f_value;
            if (totalDepth <= 1) {
                totalDepth = forwardDepth + backwardDepth + 2;
            }

            if (!dynamicSplitting ||
                this->firstForwardNodeExpanded == 0 ||
                this->firstBackwardNodeExpanded == 0) {
                forwardDepth = (totalDepth + 1) / 2;
                backwardDepth = totalDepth - forwardDepth;
            } else {
        
                double F = std::max(1.0, static_cast<double>(this->firstForwardNodeExpanded));
                double B = std::max(1.0, static_cast<double>(this->firstBackwardNodeExpanded));
                
                double forwardRatio = B / (B + F);

                //std::cout << "fr: " << forwardRatio << std::endl;

                if (!hasLearnedSplit){
                    forwardDepth = static_cast<int>(std::round(totalDepth * forwardRatio));
                    backwardDepth = totalDepth - forwardDepth;
                    hasLearnedSplit = true;
                } else {
                    // clamp ratio to avoid extreme collapse
                    //this->depthRatio = std::max(0.1, std::min(10.0, this->depthRatio));

                    int delta = totalDepth - (forwardDepth + backwardDepth);

                    forwardDepth += static_cast<int>(delta * forwardRatio);
                    //forwardDepth = std::max(1, std::min(totalDepth - 1, forwardDepth));

                    backwardDepth = totalDepth - forwardDepth;
                    //backwardDepth = std::max(1, backwardDepth);
                }

            }

            


            this->min_f_value = -1;
        }
    }

private:
    int size_in_KiB;
    int k_hashes;
    int stab_tail_len;
    int min_items;

    uint64_t  firstForwardNodeExpanded = 0;
    uint64_t  firstBackwardNodeExpanded = 0;
    uint64_t  nodeExpanded = 0;
    uint64_t totalNodesExpanded = 0;
    std::vector<IterationStat> iterStats;
    double depthRatio = 1.0;

    int min_f_value = -1;

    double time_limit; // seconds, 0 = no limit
    bool timed_out;
    int type_split_target_load = 25;
    int max_type_splits = 8;

    environment env;
};
