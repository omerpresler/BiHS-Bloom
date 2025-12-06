#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Bloom.h"
#include "MNPuzzle.h"

#define MN_SIZE 4

void init_bloom_for_puzzle(BloomFilter *bf, int size_in_KiB, int k_hashes);
void DFS(MNPuzzle<MN_SIZE, MN_SIZE> &env,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &curr, int depth,
         int targetDepth, int upperBound,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &goal, BloomFilter *bf,
         BloomFilter *existingBf,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &parent);

BloomFilter *GetBloomOfStatesInBloomAtDepth(
    MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal,
    int distance, BloomFilter *existingBf, int size_in_KiB, int k_hashes);

int solve_at_depth(MNPuzzleState<MN_SIZE, MN_SIZE> start,
                   MNPuzzleState<MN_SIZE, MN_SIZE> goal, int forwardDepth,
                   int backwardDepth, int size_in_KiB, int k_hashes,
                   int minItemsInserted, std::ostream *logFile = nullptr);

int benchmark(MNPuzzleState<MN_SIZE, MN_SIZE> start,
              MNPuzzleState<MN_SIZE, MN_SIZE> goal, int depth, int puzzle,
              std::ofstream &logFile);

std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>>
generateRandomState(int distance, int amount,
                    const MNPuzzleState<MN_SIZE, MN_SIZE> &start);

// Utility: write a puzzle state as a flat line of N*N integers.
template <int N>
void writeStateFlat(std::ostream &out, const MNPuzzleState<N, N> &state) {
  const auto &vec = state.puzzle;
  if (static_cast<int>(vec.size()) != N * N) {
    throw std::runtime_error("State board size != N*N");
  }

  for (int i = 0; i < N * N; i++) {
    if (i > 0)
      out << " ";
    out << vec[i];
  }
  out << "\n";
}
