#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>

#include "Bloom.hpp"
#include "BloomUtil.h"
#include "Timer.h"

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
    BiHSBloom(int size_in_KiB, int k_hashes)
        : size_in_KiB(size_in_KiB),
          k_hashes(k_hashes),
          stab_tail_len(4),
          min_items(10),
          env() // default-construct environment
    {}

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

    void GetStatesFromBloomRecursive(state &curr, state &goal, int depth, int targetDepth, int upperBound, BloomFilter<state>* bf, std::vector<action> &movesSoFar, std::vector<StateWithPath> &states, action lastMove) {

        if (env.HCost(curr, goal) + depth > upperBound) return;

        if (depth == targetDepth) {
            if (bf && bf->maybe_contains(curr))
                states.emplace_back(curr, movesSoFar); // vector copy is intentional
            return;
        }

        std::vector<action> moves;
        env.GetActions(curr, moves, lastMove);

        for (action a : moves) {
            env.ApplyAction(curr, a);
            movesSoFar.push_back(a);

            GetStatesFromBloomRecursive(curr, goal, depth + 1, targetDepth, upperBound, bf, movesSoFar, states, a);

            // restore
            action inv = a;
            env.InvertAction(inv);
            env.ApplyAction(curr, inv);
            movesSoFar.pop_back();
        }
    }

    std::vector<StateWithPath> GetStatesFromBloom(state &start, state &goal,int targetDepth, int upperBound, BloomFilter<state>* bf) {
        if (targetDepth == 0) {
            return {};
        }

        std::vector<StateWithPath> states;

        std::vector<action> moves;
        std::vector<action> movesSoFar;
        env.GetActions(start, moves);

        for (action a : moves) {
            env.ApplyAction(start, a);
            movesSoFar.push_back(a);

            GetStatesFromBloomRecursive(start, goal, 1, targetDepth, upperBound, bf, movesSoFar, states, a);

            // restore
            action inv = a;
            env.InvertAction(inv);
            env.ApplyAction(start, inv);
            movesSoFar.pop_back();
        }

        return states;
    }

    std::vector<action> GetPathFromBloom(state &start, state &goal, int forwardDepth, int backwardDepth, BloomFilter<state>* bf) {
        StateWithPath fState;
        StateWithPath bState;

        std::vector<StateWithPath> forwardStates;
        std::vector<StateWithPath> backwardStates;

        forwardStates = GetStatesFromBloom(start, goal, forwardDepth, forwardDepth + backwardDepth, bf);
        backwardStates = GetStatesFromBloom(goal, start, backwardDepth, forwardDepth + backwardDepth, bf);

        for (auto &s : forwardStates) {
            for (auto &s2 : backwardStates) {
                if (s.first == s2.first) {
                    fState = s;
                    bState = s2;
                    break;
                }
            }
        }

        std::vector<action> path;

        for (auto it = fState.second.begin(); it != fState.second.end(); ++it) {
            path.push_back(*it);
        }

        for (auto it = bState.second.rbegin(); it != bState.second.rend(); ++it) {
            env.InvertAction(*it);
            path.push_back(*it);
        }
        return path;
    }

    void BuildBloomFrontierRecursive(state &curr, state &goal, int depth, int targetDepth, int upperBound, BloomFilter<state>* oldBf, BloomFilter<state>* newBf, action last_action) {

        if (env.HCost(curr, goal) + depth > upperBound) { // If f value > upper bound no need to explore
            return;
        }
        
        // If we are at the correct depth, check if the state is in the Bloom filter, if Bloom filter is null no need to check we'll add every state at depth
        if (depth == targetDepth) {
            if (!oldBf || oldBf->maybe_contains(curr))
                newBf->add(curr);
            return; // CRITICAL: do not expand deeper
        }
            
        std::vector<action> actions;
        env.GetActions(curr, actions, last_action);

        for (auto a : actions) {
            env.ApplyAction(curr, a);
            BuildBloomFrontierRecursive(curr, goal, depth + 1, targetDepth, upperBound, oldBf, newBf, a);

            env.InvertAction(a); // "Fix" the state
            env.ApplyAction(curr, a);
        }
    }

    void BuildBloomFrontier(state &start, state &goal, int targetDepth, int upperBound, BloomFilter<state>* oldBf, BloomFilter<state>* newBf) {
        std::vector<action> actions;
        env.GetActions(start, actions); // no lastAction at root

        for (auto a : actions) {
            env.ApplyAction(start, a);
            BuildBloomFrontierRecursive(start, goal, 1, targetDepth, upperBound, oldBf, newBf, a);

            env.InvertAction(a); // "Fix" the state
            env.ApplyAction(start, a);
        }
    }

    BloomFilter<state>* GetBloomOfStatesInBloomAtDepth(state start, state goal, int depth, int upperBound, BloomFilter<state>* oldBf) {

        BloomFilter<state> *bf = nullptr;
        InitBloom(bf);
        BuildBloomFrontier(start, goal, depth, upperBound, oldBf, bf);

        return bf;
    }

    std::vector<action> SolveAtDepth(state start, state &goal, int forwardDepth, int backwardDepth) {
        Period2Window<LOOP_LIMIT> tail;
        TerminationCondition term = TerminationCondition::MAX_ITERATIONS;
        std::unique_ptr<BloomFilter<state>> bf;
        std::vector<action> path;
        int upperBound = forwardDepth + backwardDepth; // We know f value from node to goal cant be bigger then Df + Db
        Timer solveTimer;
        solveTimer.StartTimer();
        
        //std::cout << "[PROF] Starting SolveAtDepth fd=" << forwardDepth << " bd=" << backwardDepth << " ub=" << upperBound << std::endl; 

        for(int i = 0; i < LOOP_LIMIT && term == TerminationCondition::MAX_ITERATIONS; i++) {
            Timer iterTimer;
            iterTimer.StartTimer();

            if (tail.last_n_period2()){ //Period 2 stabilized, we can stop the algorithm
                term = TerminationCondition::STABILIZED;
                //std::cout << "[PROF] Stabilized at iter " << i << std::endl;
                break;
            }

            if (i % 2 == 0)
                bf.reset(GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, upperBound, bf.get()));
            else
                bf.reset(GetBloomOfStatesInBloomAtDepth(goal, start, backwardDepth, upperBound, bf.get()));

            tail.push(bf->get_n_inserted());
            
            iterTimer.EndTimer();
            //std::cout << "[PROF] Iter " << i << " items=" << bf->get_n_inserted() << " time=" << iterTimer.GetElapsedTime() << "s" << " fp=" << bf->estimate_fp() << std::endl;

            if (bf->get_n_inserted() <= this->min_items){ //Bloom is small enopugh that we can save the states in memory
                term = TerminationCondition::MIN_ITEMS;
                //std::cout << "[PROF] Min items reached at iter " << i << std::endl;
                break;
            }
        }

        solveTimer.EndTimer();
        //std::cout << "[PROF] BuildBloom loop finished in " << solveTimer.GetElapsedTime() << "s. Starting GetPathFromBloom." << std::endl;

        Timer pathTimer;
        pathTimer.StartTimer();
        path = GetPathFromBloom(start, goal, forwardDepth, backwardDepth, bf.get());
        pathTimer.EndTimer();
        //std::cout << "[PROF] GetPathFromBloom time=" << pathTimer.GetElapsedTime() << "s" << std::endl;

        return path;
    }

    std::vector<action> GetPath(state start, state goal) {
        int fh = env.HCost(start, goal);
        int bh = env.HCost(goal, start);
        int distance = std::max(fh, bh);
        int forwardDepth = distance / 2;
        int backwardDepth = distance - forwardDepth;
        Timer totalTimer;
        totalTimer.StartTimer();


        while(true){
            //std::cout << "[PROF] Trying depth: " << forwardDepth << " + " << backwardDepth << " = " << (forwardDepth + backwardDepth) << std::endl;
            std::vector<action> path = SolveAtDepth(start, goal, forwardDepth, backwardDepth);

            if (path.size() > 0){
                SanityCheck(start, goal, path);
                totalTimer.EndTimer();
                //std::cout << "[PROF] Total GetPath time=" << totalTimer.GetElapsedTime() << "s" << std::endl;
                return path;
            }
            
            if (forwardDepth == backwardDepth) // up to this point we assume Cstar is fd + bd, if we reached here no solution was found, we increment bd first.
                backwardDepth++;
            else
                forwardDepth++;   
        }
    }

private:
    int size_in_KiB;
    int k_hashes;
    int stab_tail_len;
    int min_items;

    environment env;
};