#pragma once

#include <iostream>
#include <vector>
#include "MNPuzzle.h"       // for MNPuzzleState declaration

#define MN_SIZE 4

int solve(MNPuzzleState<MN_SIZE, MN_SIZE> start,
          MNPuzzleState<MN_SIZE, MN_SIZE> goal);

std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>>
generateRandomState(int distance,
                    int amount,
                    const MNPuzzleState<MN_SIZE, MN_SIZE> &start);

// Template *must* be fully defined in the header if used from other TUs
template <int N>
void writeStateFlat(std::ostream &out,
                    const MNPuzzleState<N, N> &state)
{
    const auto &vec = state.puzzle;   // uses your 1D vector/array field
    if (static_cast<int>(vec.size()) != N * N) {
        throw std::runtime_error("State board size != N*N");
    }

    for (int i = 0; i < N * N; i++) {
        if (i > 0) out << " ";
        out << vec[i];
    }
    out << "\n";
}
