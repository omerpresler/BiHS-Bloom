#ifndef BIHS_BLOOM_H
#define BIHS_BLOOM_H

#include "MNPuzzle.h"
#include "Bloom.hpp"

#define MN_SIZE 4

std::vector<slideDir> solveBloom(MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal);

#endif