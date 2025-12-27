#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

//#include "Bloom.h"
#include "MNPuzzle.h"
#include "bihsBloom.h"

#define MN_SIZE 4

void init_bloom_for_puzzle(BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *&bf,
                           int size_in_KiB, int k_hashes,
                           BloomType type = BloomType::REGULAR,
                           double set_ratio = 0.0);
void DFS(MNPuzzle<MN_SIZE, MN_SIZE> &env,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &curr, int depth,
         int targetDepth, int upperBound,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &goal, BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *bf,
         BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *existingBf,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &parent);

BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *GetBloomOfStatesInBloomAtDepth(
    MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal,
    int distance, BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *existingBf,
    int size_in_KiB, int k_hashes, BloomType type, double set_ratio);

int solve_at_depth(MNPuzzleState<MN_SIZE, MN_SIZE> start,
                   MNPuzzleState<MN_SIZE, MN_SIZE> goal, int forwardDepth,
                   int backwardDepth, int size_in_KiB, int k_hashes,
                   int minItemsInserted, BloomType bloomType, double set_ratio,
                   std::ostream *logFile = nullptr);

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
