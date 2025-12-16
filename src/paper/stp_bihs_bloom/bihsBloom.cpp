/*
 *  $Id: bihsBloom.c
 *  Biderctional Heuristic search with Bloom filter to save the frontier
 *
 *  Created by Omer Presler on 12/06/25.
 *
 * 
 *
 */ 

#include "bihsBloom.h"
#include <vector>
#include <algorithm>

#define BLOOM_SIZE_IN_KiB 1024
#define BLOOM_K_HASHES 3
#define MIN_BLOOM_SIZE 5

using StateWithPath = std::pair<MNPuzzleState<MN_SIZE, MN_SIZE>, std::vector<slideDir>>;

void GetStatesInBloomAtDepth(
    MNPuzzleState<MN_SIZE, MN_SIZE> curr, MNPuzzleState<MN_SIZE, MN_SIZE> goal,
    int depth, int cost, BloomFilter *bf, std::vector<StateWithPath> &states,
    std::vector<slideDir> &movesSoFar) {
    MNPuzzle<MN_SIZE, MN_SIZE> env;
    double h = env.HCost(curr, goal);
    double f = depth + h;
    if (f > cost) {
        return;
    }

    if (depth == cost && bf->maybe_contains(&curr.puzzle, sizeof(curr.puzzle))) {
        states.push_back(std::make_pair(curr, movesSoFar)); // std::maker pair supposedly copies the vector, not sure if that's true
    }

    std::vector<slideDir> moves;
    if (movesSoFar.empty()){
        env.GetActions(curr, moves);
    } else {
        env.GetActions(curr, moves, movesSoFar.back());
    }

    for (unsigned int y = 0; y < moves.size(); y++)
	{
        env.ApplyAction(curr, moves[y]);
        movesSoFar.push_back(moves[y]);
        GetStatesInBloomAtDepth(curr, goal, depth + 1, cost, bf, states, movesSoFar);
        env.InvertAction(moves[y]);
        env.ApplyAction(curr, moves[y]); // "restore" the state, saves memory since we pass the same puzzle to the next call
        movesSoFar.pop_back();
    }
    
}

void IDAStarWithBloom(
    MNPuzzleState<MN_SIZE, MN_SIZE> curr, MNPuzzleState<MN_SIZE, MN_SIZE> goal,
    int depth, int targetDepth, int upperBound, BloomFilter *existingBf, BloomFilter *bf, slideDir lastMove) {

    MNPuzzle<MN_SIZE, MN_SIZE> env;
    double h = env.HCost(curr, goal);
    double f = depth + h;
    if (f > upperBound) { //upperBound is forward depth + backward depth since we run it iterativley we assume that the true distance is fd + bd
        return;
    }

    if (depth == targetDepth) {
        if (existingBf == nullptr) {
            bf->add(&curr.puzzle, sizeof(curr.puzzle));
        } else {
            if (existingBf->maybe_contains(&curr.puzzle, sizeof(curr.puzzle))) {
                bf->add(&curr.puzzle, sizeof(curr.puzzle));
            }
        }
        return;
    }

    std::vector<slideDir> moves;
    env.GetActions(curr, moves, lastMove);

    for (unsigned int y = 0; y < moves.size(); y++)
	{
        env.ApplyAction(curr, moves[y]);
        IDAStarWithBloom(curr, goal, depth + 1, targetDepth, upperBound, existingBf, bf, moves[y]);
        env.InvertAction(moves[y]);
        env.ApplyAction(curr, moves[y]); // "restore" the state, saves memory since we pass the same puzzle to the next call
    }
    
}


BloomFilter *GetBloomOfStatesInBloomAtDepth(
    MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal,
    int depth, int upperBound, BloomFilter *existingBf) {

  size_t m_bits = BLOOM_SIZE_IN_KiB * 1024 * 8ULL;
  BloomFilter *bf = new BloomFilter(m_bits, BLOOM_K_HASHES);

  IDAStarWithBloom(start, goal, 0, depth, upperBound, existingBf, bf, kNoSlide);

  return bf;
}

std::vector<slideDir> solveBloom(MNPuzzleState<MN_SIZE, MN_SIZE> start,
          MNPuzzleState<MN_SIZE, MN_SIZE> goal) {
  
  MNPuzzle<MN_SIZE, MN_SIZE> env;
  
  int minDistance = env.HCost(start, goal); // Heuristic distance can under-estimate the true distance but not over-estimate it

  std::cout << "Heuristic distance: " << minDistance << std::endl;

  int forwardDepth = minDistance / 2;
  int backwardDepth = minDistance - forwardDepth;
  bool forward = true;
  BloomFilter *bf = nullptr; // Initialize to nullptr

  while(true){
    std::cout << "Forward depth: " << forwardDepth << std::endl;
    std::cout << "Backward depth: " << backwardDepth << std::endl;
    
    do{ 
        if (bf) {
             std::cout << "Items inserted: " << bf->get_n_inserted() << std::endl;
        } else {
             std::cout << "Items inserted: 0" << std::endl;
        }

        if(forward){
        bf = GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, backwardDepth, nullptr);
        }else{
        bf = GetBloomOfStatesInBloomAtDepth(goal, start, forwardDepth, backwardDepth, nullptr);
        }
        forward = !forward;

    }while(bf->get_n_inserted() > MIN_BLOOM_SIZE);

    //At this point we have a bloom filter that holds minimal number of states we suspect are on the path, lets run IDA* with them (we know their depth)
    if (bf->get_n_inserted() > 0){

        std::vector<slideDir> moves = std::vector<slideDir>();


        std::vector<StateWithPath> forwardStates;
        GetStatesInBloomAtDepth(start, goal, forwardDepth, minDistance, bf, forwardStates, moves);

        moves.clear();

        std::vector<StateWithPath> backwardStates;
        GetStatesInBloomAtDepth(goal, start, backwardDepth, minDistance, bf, backwardStates, moves);

        // In theory forwardStates and backwardStates should be identical, in  practive we want them to intersect at least once

        std::vector<slideDir> path;

        for (auto &fState : forwardStates) {
            for (auto &bState : backwardStates) {
            if (fState.first == bState.first) {
                // forward path: copy as-is
                path.insert(path.end(), fState.second.begin(), fState.second.end());

                // backward path: go in reverse and invert each action
                for (auto it = bState.second.rbegin(); it != bState.second.rend(); ++it) {
                    slideDir inverted = *it;
                    env.InvertAction(inverted);
                    path.push_back(inverted);
                }
                
                delete bf; // Clean up
                return path;
            }
            }
        }
    }
    
    if (bf) {
        delete bf; // Clean up before next iteration
        bf = nullptr;
    }

    if (forwardDepth == backwardDepth){ // up to this point we assume Cstar is fd + bd, if we reached here no solution was found, we increment bd first.
      backwardDepth++;
    }else{
      forwardDepth++;
    }
  }
  
}