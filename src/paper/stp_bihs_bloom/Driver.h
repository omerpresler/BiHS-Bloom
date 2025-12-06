#pragma once

#include <iostream>
#include <vector>
#include <stdexcept>
#include <string>

#include "MNPuzzle.h"
#include "Bloom.h"

#define MN_SIZE 4

// Initialize a Bloom filter for the puzzle domain.
// size_in_MiB: memory budget in MiB (converted to bits inside).
// k_hashes: number of hash functions.
void init_bloom_for_puzzle(BloomFilter* bf,
                           unsigned long long size_in_MiB,
                           int k_hashes);

// Build a Bloom filter containing all states at a given distance from `start`
// that ALSO appear in `existingBf` (if non-null). Results are added into `bf`.
// Returns *bf by value for convenience.
BloomFilter GetBloomOfStatesInBloomAtDepth(
    MNPuzzleState<MN_SIZE, MN_SIZE> start,
    MNPuzzleState<MN_SIZE, MN_SIZE> goal,
    int distance,
    BloomFilter* existingBf,
    BloomFilter* bf);

// Solve a single puzzle instance from `start` to `goal`.
// Currently uses Bloom-based frontier generation in your Driver.cpp.
int solve(MNPuzzleState<MN_SIZE, MN_SIZE> start,
          MNPuzzleState<MN_SIZE, MN_SIZE> goal);

// Generate `amount` random states at exactly `distance` moves
// (optimal distance) from `start`, using BFS.
std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>>
generateRandomState(int distance,
                    int amount,
                    const MNPuzzleState<MN_SIZE, MN_SIZE> &start);

// Utility: write a puzzle state as a flat line of N*N integers.
template <int N>
void writeStateFlat(std::ostream &out,
                    const MNPuzzleState<N, N> &state)
{
    const auto &vec = state.puzzle;
    if (static_cast<int>(vec.size()) != N * N) {
        throw std::runtime_error("State board size != N*N");
    }

    for (int i = 0; i < N * N; i++) {
        if (i > 0) out << " ";
        out << vec[i];
    }
    out << "\n";
}
