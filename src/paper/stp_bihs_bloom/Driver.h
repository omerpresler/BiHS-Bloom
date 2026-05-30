#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>

#include "Bloom.h"
#include "MNPuzzle.h"

#define MN_SIZE 4

void solveSTP();
void solvePancake();

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
