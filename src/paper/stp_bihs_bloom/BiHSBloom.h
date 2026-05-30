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
}

#define LOOP_LIMIT 200
#define BRANCH_FACTOR 4

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
    };

    bool hasTimedOut() const { return timed_out; }
    size_t GetTotalNodesExpanded() const { return totalNodesExpanded; }
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

    void GetStatesFromBloomRecursive(state &curr, state &goal, uint64_t currHash, int depth, int targetDepth, int upperBound, BloomFilter<state>* bf, std::vector<action> &movesSoFar, std::vector<StateWithPath> &states, action lastMove) {

        if (env.HCost(curr, goal) + depth > upperBound) return;

        if (depth == targetDepth) {
            if (bf && bf->maybe_contains_hash(currHash))
                states.emplace_back(curr, movesSoFar); // vector copy is intentional
            return;
        }

        std::vector<action> moves;
        BiHSBloomHelper::get_actions(env, curr, moves, lastMove, 0);

        for (action a : moves) {
            uint64_t nextHash = currHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(currHash, curr, a);
            }
            env.ApplyAction(curr, a);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(curr);
            }
            movesSoFar.push_back(a);

            GetStatesFromBloomRecursive(curr, goal, nextHash, depth + 1, targetDepth, upperBound, bf, movesSoFar, states, a);

            // restore
            action inv = a;
            env.InvertAction(inv);
            env.ApplyAction(curr, inv);
            movesSoFar.pop_back();
        }
    }

    std::vector<StateWithPath> GetStatesFromBloom(state &start, state &goal,
                                                int targetDepth, int upperBound,
                                                BloomFilter<state>* bf)
    {
        uint64_t startHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        if (targetDepth == 0) {
            if (bf && bf->maybe_contains_hash(startHash) && env.HCost(start, goal) <= upperBound) {
                return {{start, {}}};
            }
            return {};
        }

        std::vector<StateWithPath> states;
        std::vector<action> moves;
        std::vector<action> movesSoFar;
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
                                        upperBound, bf, movesSoFar, states, a);

            action inv = a;
            env.InvertAction(inv);
            env.ApplyAction(start, inv);
            movesSoFar.pop_back();
        }

        return states;
    }

    PathExtractionResult GetPathFromBloom(state &start, state &goal,
                                    int forwardDepth, int backwardDepth,
                                    BloomFilter<state>* bf)
    {
        auto forwardStates = GetStatesFromBloom(start, goal, forwardDepth,
                                                forwardDepth + backwardDepth, bf);
        auto backwardStates = GetStatesFromBloom(goal, start, backwardDepth,
                                                forwardDepth + backwardDepth, bf);

        std::unordered_map<uint64_t, std::vector<action>> backwardMap;
        backwardMap.reserve(backwardStates.size() * 2);

        for (auto &s : backwardStates) {
            uint64_t h = env.GetStateHash(s.first);
            if (backwardMap.find(h) == backwardMap.end()) {
                backwardMap.emplace(h, s.second);
            }
        }

        for (auto &f : forwardStates) {
            uint64_t h = env.GetStateHash(f.first);
            auto it = backwardMap.find(h);
            if (it != backwardMap.end()) {
                std::vector<action> path;
                path.reserve(f.second.size() + it->second.size());

                for (const auto &a : f.second) {
                    path.push_back(a);
                }

                for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
                    action inv = *rit;
                    env.InvertAction(inv);
                    path.push_back(inv);
                }

                return {path, forwardStates.size(), backwardStates.size()};
            }
        }

        return {{}, forwardStates.size(), backwardStates.size()};
    }

    void BuildBloomFrontierRecursive(state &curr, state &goal, uint64_t currHash, int depth, int targetDepth, int upperBound, BloomFilter<state>* oldBf, BloomFilter<state>* newBf, action last_action) {

        size_t f_value = env.HCost(curr, goal) + depth;
        if (f_value > upperBound) { // If f value > upper bound no need to explore
            if (this->min_f_value == -1 || this->min_f_value > f_value)
                    this->min_f_value = f_value;
            return;
        }
        
        // If we are at the correct depth, check if the state is in the Bloom filter, if Bloom filter is null no need to check we'll add every state at depth
        if (depth == targetDepth) {
            this->nodeExpanded++;
            if (!oldBf || oldBf->maybe_contains_hash(currHash))
                newBf->add_hash(currHash);
            return; // CRITICAL: do not expand deeper
        }
            
        std::vector<action> actions;
        BiHSBloomHelper::get_actions(env, curr, actions, last_action, 0);

        for (auto a : actions) {
            uint64_t nextHash = currHash;
            if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(currHash, curr, a);
            }
            env.ApplyAction(curr, a);
            if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(curr);
            }
            BuildBloomFrontierRecursive(curr, goal, nextHash, depth + 1, targetDepth, upperBound, oldBf, newBf, a);

            env.InvertAction(a); // "Fix" the state
            env.ApplyAction(curr, a);
        }
    }

    struct Frame {
        std::vector<action> acts;
        uint64_t hash = 0;
        uint8_t next = 0;
        action last;
        bool has_last;

        Frame(uint64_t hash) : hash(hash), next(0), has_last(false) {
            acts.reserve(BRANCH_FACTOR); // IMPORTANT: prevents reallocations
        }

        Frame(action lastAction, uint64_t hash) : hash(hash), next(0), last(lastAction), has_last(true) {
            acts.reserve(BRANCH_FACTOR); // IMPORTANT: prevents reallocations
        }
    };

    void BuildBloomFrontier(
        state &start,
        state &goal,
        int targetDepth,
        int upperBound,
        BloomFilter<state>* oldBf,
        BloomFilter<state>* newBf)
    {
        std::vector<Frame> st;
        st.reserve(targetDepth + 1);
        state curr = start;
        uint64_t currHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
        st.emplace_back(currHash);

        while (!st.empty()) {
            this->nodeExpanded++;
            Frame &f = st.back();
            int depth = (int)st.size() - 1;

            size_t f_value = env.HCost(curr, goal) + depth;
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

            // If we just arrived to this frame, generate actions once
            if (f.next == 0 && f.acts.empty()) {
                f.acts.clear();
                if (f.has_last) {
                    BiHSBloomHelper::get_actions(env, curr, f.acts, f.last, 0);
                } else {
                    env.GetActions(curr, f.acts);
                }
            }

            // Target depth: add to Bloom and backtrack (don’t expand deeper)
            if (depth == targetDepth) {
                if (!oldBf || oldBf->maybe_contains_hash(currHash))
                    newBf->add_hash(currHash);

                if (st.size() == 1) break;
                action undo = f.last;
                st.pop_back();
                env.UndoAction(curr, undo);
                currHash = st.back().hash;
                continue;
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
            st.emplace_back(a, currHash); // child frame "remembers" last action
        }
    }

    BloomFilter<state>* GetBloomOfStatesInBloomAtDepth(state start, state goal, int depth, int upperBound, BloomFilter<state>* oldBf, bool recursive) {

        BloomFilter<state> *bf = nullptr;
        InitBloom(bf);
        this->nodeExpanded = 0;
        uint64_t startHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);

        if (depth == 0) {
            this->nodeExpanded++;
            if (!oldBf || oldBf->maybe_contains_hash(startHash)) {
                bf->add_hash(startHash);
            }
            this->totalNodesExpanded += this->nodeExpanded;
            return bf;
        }
        

        if(recursive){
            std::vector<action> actions;
            env.GetActions(start, actions); // no lastAction at root

            for (auto a : actions) {
                uint64_t nextHash = startHash;
                if (BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                    nextHash = BiHSBloomHelper::StateFingerprint<state, action>::apply(startHash, start, a);
                }
                env.ApplyAction(start, a);
                if (!BiHSBloomHelper::StateFingerprint<state, action>::incremental) {
                    nextHash = BiHSBloomHelper::StateFingerprint<state, action>::hash(start);
                }
                BuildBloomFrontierRecursive(start, goal, nextHash, 1, depth, upperBound, oldBf, bf, a);

                env.InvertAction(a); // "Fix" the state
                env.ApplyAction(start, a);
            }
        }
        else{
            BuildBloomFrontier(start, goal, depth, upperBound, oldBf, bf);
        }
        

        this->totalNodesExpanded += this->nodeExpanded;
        return bf;
    }


    std::vector<action> SolveAtDepth(state start, state &goal, int forwardDepth, int backwardDepth, bool recursive, Timer &globalTimer) {
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
                bf.reset(GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, upperBound, bf.get(), recursive));
                lastForwardInserted = bf->get_n_inserted();
                haveForward = true;
                if(this->firstForwardNodeExpanded == 0)
                    this->firstForwardNodeExpanded = this->nodeExpanded;
            }
            else {
                bf.reset(GetBloomOfStatesInBloomAtDepth(goal, start, backwardDepth, upperBound, bf.get(), recursive));
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
                0
            });

            
            iterTimer.EndTimer();
            {
                std::ofstream proof_log("proof_unique.csv", std::ios::app);
                proof_log << (forwardDepth + backwardDepth) << "," << i << ","
                          << bf->get_n_inserted() << "," << bf->get_n_unique() << ","
                          << bf->get_bits_set() << "," << bf->get_fill_ratio() << ","
                          << bf->expected_fill_ratio() << "\n";
            }

            if (haveForward && haveBackward &&
                lastForwardInserted + lastBackwardInserted <= static_cast<size_t>(this->min_items)) {
                term = TerminationCondition::MIN_ITEMS;
                //std::cout << "[PROF] Min items reached at iter " << i << std::endl;
                break;
            }
        }

        //std::cout << "[PROF] BuildBloom loop finished. Starting GetPathFromBloom." << std::endl;

        Timer pathTimer;
        pathTimer.StartTimer();
        PathExtractionResult extraction = GetPathFromBloom(start, goal, forwardDepth, backwardDepth, bf.get());
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

    std::vector<action> GetPath(state start, state goal, bool recursive=false) {
        timed_out = false;
        totalNodesExpanded = 0;
        iterStats.clear();
        int fh = env.HCost(start, goal);
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
            std::vector<action> path = SolveAtDepth(start, goal, forwardDepth, backwardDepth, recursive, globalTimer);

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

            if (!dynamicSplitting) {
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

    int firstForwardNodeExpanded = 0;
    int firstBackwardNodeExpanded = 0;
    int nodeExpanded;
    size_t totalNodesExpanded = 0;
    std::vector<IterationStat> iterStats;
    double depthRatio = 1.0;

    int min_f_value = -1;

    double time_limit; // seconds, 0 = no limit
    bool timed_out;

    environment env;
};
