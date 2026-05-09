#include "Driver.h"

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
  require(bloom.get_bits_set() > 0, "regular Bloom did not set any bits");

  bloom.clear();
  require(bloom.get_n_inserted() == 0, "regular Bloom clear did not reset inserted count");
  require(bloom.get_n_unique() == 0, "regular Bloom clear did not reset unique count");
  require(!bloom.maybe_contains(goal), "regular Bloom clear left goal present");
}

void testInitBloomForPuzzleCreatesExpectedFilterTypes() {
  BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *regular = nullptr;
  init_bloom_for_puzzle(regular, 1, 2, BloomType::REGULAR, 0.0);
  std::unique_ptr<BloomFilter<std::array<int, MN_SIZE * MN_SIZE>>> regularPtr(regular);

  require(regularPtr != nullptr, "regular init returned null");
  require(dynamic_cast<BloomFilterWithSet<MNPuzzle<MN_SIZE, MN_SIZE>,
                                         std::array<int, MN_SIZE * MN_SIZE>> *>(
              regularPtr.get()) == nullptr,
          "regular init returned Bloom-with-set");

  BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *withSet = nullptr;
  init_bloom_for_puzzle(withSet, 1, 2, BloomType::WITH_SET, 0.10);
  std::unique_ptr<BloomFilter<std::array<int, MN_SIZE * MN_SIZE>>> withSetPtr(withSet);

  auto *typed = dynamic_cast<BloomFilterWithSet<MNPuzzle<MN_SIZE, MN_SIZE>,
                                               std::array<int, MN_SIZE * MN_SIZE>> *>(
      withSetPtr.get());
  require(typed != nullptr, "with-set init did not return Bloom-with-set");
  require(typed->get_set_limit() > 0, "with-set init produced an empty set limit");
}

void testDepthZeroBloomContainsStart() {
  MNPuzzleState<MN_SIZE, MN_SIZE> start;
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  MNPuzzle<MN_SIZE, MN_SIZE> env;
  start.Reset();
  goal.Reset();

  std::unique_ptr<BloomFilter<std::array<int, MN_SIZE * MN_SIZE>>> bloom(
      GetBloomOfStatesInBloomAtDepth(start, goal, env, 0, nullptr, 1, 2,
                                     BloomType::REGULAR, 0.0));

  require(bloom != nullptr, "depth-zero Bloom returned null");
  require(bloom->get_n_inserted() == 1, "depth-zero Bloom inserted count is wrong");
  require(bloom->maybe_contains(start.puzzle), "depth-zero Bloom does not contain start");
}

void testBloomCapacityTracksFilledBits() {
  constexpr size_t bloomBytes = 100;
  constexpr size_t bloomBits = bloomBytes * 8;
  constexpr size_t expectedBitsOn = 100;

  BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> bloom(bloomBits, 1);

  size_t acceptedItems = 0;
  for (uint64_t candidate = 0; acceptedItems < expectedBitsOn; ++candidate) {
    const auto item = deterministicItem(candidate);
    if (bloom.maybe_contains(item)) {
      continue;
    }

    const size_t bitsBefore = bloom.get_bits_set();
    bloom.add(item);
    ++acceptedItems;

    require(bloom.get_bits_set() == bitsBefore + 1,
            "k=1 Bloom accepted item did not turn on exactly one new bit");
  }

  require(bloom.get_n_inserted() == expectedBitsOn,
          "Bloom capacity test inserted count is wrong");
  require(bloom.get_bits_set() == expectedBitsOn,
          "Bloom capacity test has the wrong number of bits on");
}

} // namespace

int main() {
  const auto tests = {
      testRegularBloomFilterTracksInsertedValues,
      testInitBloomForPuzzleCreatesExpectedFilterTypes,
      testDepthZeroBloomContainsStart,
      testBloomCapacityTracksFilledBits,
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
