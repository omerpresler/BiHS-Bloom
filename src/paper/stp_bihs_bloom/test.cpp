#include "Driver.h"
#include "BiHSBloom.h"
#include "AStarOpenClosed.h"
#include <ext/hash_map>
#include "IDTHSwTrans.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

class TestFailure : public std::runtime_error {
public:
  explicit TestFailure(const std::string &message) : std::runtime_error(message) {}
};

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw TestFailure(message);
  }
}

std::array<int, MN_SIZE * MN_SIZE> goalPuzzleArray() {
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  goal.Reset();
  return goal.puzzle;
}

std::array<int, MN_SIZE * MN_SIZE> deterministicItem(uint64_t value) {
  std::array<int, MN_SIZE * MN_SIZE> item{};
  uint64_t x = value + 0x9e3779b97f4a7c15ULL;

  for (auto &entry : item) {
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    entry = static_cast<int>((x * 0x2545f4914f6cdd1dULL) & 0x7fffffff);
  }

  return item;
}

void testRegularBloomFilterTracksInsertedValues() {
  const auto goal = goalPuzzleArray();

  BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> bloom(1024 * 8, 3);
  require(!bloom.maybe_contains(goal), "empty regular Bloom should not contain goal");

  bloom.add(goal);

  require(bloom.maybe_contains(goal), "regular Bloom lost an inserted value");
  require(bloom.get_n_inserted() == 1, "regular Bloom inserted count is wrong");
  require(bloom.get_n_unique() == 1, "regular Bloom unique count is wrong");

  bloom.clear();
  require(bloom.get_n_inserted() == 0, "regular Bloom clear did not reset inserted count");
  require(bloom.get_n_unique() == 0, "regular Bloom clear did not reset unique count");
  require(!bloom.maybe_contains(goal), "regular Bloom clear left goal present");
}

void testDepthZeroBloomContainsStart() {
  MNPuzzleState<MN_SIZE, MN_SIZE> start;
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  start.Reset();
  goal.Reset();

  BiHSBloom<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir,
            MNPuzzle<MN_SIZE, MN_SIZE>>
      solver(1, 2);

  std::unique_ptr<BloomFilter<MNPuzzleState<MN_SIZE, MN_SIZE>>> bloom(
      solver.GetBloomOfStatesInBloomAtDepth(start, goal, 0, 0, nullptr));

  require(bloom != nullptr, "depth-zero Bloom returned null");
  require(bloom->get_n_inserted() == 1, "depth-zero Bloom inserted count is wrong");
  require(bloom->maybe_contains(start), "depth-zero Bloom does not contain start");
}

void testBloomTracksAcceptedItems() {
  constexpr size_t bloomBytes = 100;
  constexpr size_t bloomBits = bloomBytes * 8;
  constexpr size_t expectedItems = 100;

  BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> bloom(bloomBits, 1);

  size_t acceptedItems = 0;
  for (uint64_t candidate = 0; acceptedItems < expectedItems; ++candidate) {
    const auto item = deterministicItem(candidate);
    if (bloom.maybe_contains(item)) {
      continue;
    }

    bloom.add(item);
    ++acceptedItems;

    require(bloom.maybe_contains(item), "Bloom lost an accepted item");
  }

  require(bloom.get_n_inserted() == expectedItems,
          "Bloom accepted-item test inserted count is wrong");
}

void testBiHSBloomCollectsZeroDepthState() {
  MNPuzzleState<MN_SIZE, MN_SIZE> start;
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  start.Reset();
  goal.Reset();

  BloomFilter<MNPuzzleState<MN_SIZE, MN_SIZE>> bloom(1024 * 8, 3);
  bloom.add(start);

  BiHSBloom<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir,
            MNPuzzle<MN_SIZE, MN_SIZE>>
      solver(1, 3);

  auto states = solver.GetStatesFromBloom(start, goal, 0, 0, &bloom);

  require(states.size() == 1, "BiHS-Bloom zero-depth collection missed start");
  auto stateIt = states.begin();
  require(stateIt->second.first == start,
          "BiHS-Bloom zero-depth collection returned the wrong state");
  require(stateIt->second.second.empty(),
          "BiHS-Bloom zero-depth path should be empty");
}

void testBiHSBloomSolvesOneMovePuzzle() {
  MNPuzzleState<MN_SIZE, MN_SIZE> start;
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  MNPuzzle<MN_SIZE, MN_SIZE> env;
  start.Reset();
  goal.Reset();

  std::vector<slideDir> actions;
  env.GetActions(start, actions);
  require(!actions.empty(), "goal state unexpectedly has no actions");
  env.ApplyAction(start, actions.front());

  BiHSBloom<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir,
            MNPuzzle<MN_SIZE, MN_SIZE>>
      solver(1, 3);

  auto path = solver.GetPath(start, goal);
  require(path.size() == 1, "BiHS-Bloom failed to solve a one-move puzzle");

  MNPuzzleState<MN_SIZE, MN_SIZE> check = start;
  for (slideDir action : path) {
    env.ApplyAction(check, action);
  }
  require(check == goal, "BiHS-Bloom one-move solution does not reach goal");
}

void testIDTHSwTransSolvesOneMovePuzzle() {
  MNPuzzleState<MN_SIZE, MN_SIZE> start;
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  MNPuzzle<MN_SIZE, MN_SIZE> env;
  start.Reset();
  goal.Reset();

  std::vector<slideDir> actions;
  env.GetActions(start, actions);
  require(!actions.empty(), "goal state unexpectedly has no actions");
  env.ApplyAction(start, actions.front());

  IDTHSwTrans<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, false> solver(false, true, true, 1, false);
  bool solved = solver.GetPath(&env, start, goal, 5, 128);

  require(solved, "IDTHSwTrans failed to solve a one-move puzzle");
  require(solver.getPathLength() == 1, "IDTHSwTrans one-move solution cost is wrong");
}

} // namespace

int main() {
  const auto tests = {
      testRegularBloomFilterTracksInsertedValues,
      testDepthZeroBloomContainsStart,
      testBloomTracksAcceptedItems,
      testBiHSBloomCollectsZeroDepthState,
      testBiHSBloomSolvesOneMovePuzzle,
      testIDTHSwTransSolvesOneMovePuzzle,
  };

  int passed = 0;
  for (auto test : tests) {
    try {
      test();
      ++passed;
    } catch (const TestFailure &error) {
      std::cerr << "Test failed: " << error.what() << "\n";
      return 1;
    }
  }

  std::cout << "All " << passed << " stp_bihs_bloom tests passed.\n";
  return 0;
}
