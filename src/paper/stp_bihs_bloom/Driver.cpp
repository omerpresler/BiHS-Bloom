#include "Driver.h"
#include "IncrementalIDA.h"
#include "MNPuzzle.h"
#include "TemplateAStar.h"
#include "STPInstances.h"
#include "Timer.h"
#include "BiHSBloom.h"
#include "BAE.h"
#include "MM.h"
#include "IDAStar.h"

#include "PancakeInstances.h"

#include <algorithm>
#include <random>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <future>
#include <atomic>
#include <memory>

void init_bloom_for_puzzle(BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *&bf,
                           int size_in_KiB, int k_hashes, BloomType type,
                           double set_ratio) { // We set default value in Driver.h

  size_t m_bits = size_in_KiB * 1024 * 8ULL;
  if (type == BloomType::WITH_SET) {
    size_t set_limit =
        static_cast<size_t>(m_bits * set_ratio / sizeof(MNPuzzleState<MN_SIZE, MN_SIZE>));
    if (set_limit == 0) {
      set_limit = 1;
    }
    bf = new BloomFilterWithSet<MNPuzzle<MN_SIZE, MN_SIZE>, std::array<int, MN_SIZE * MN_SIZE>>(
        m_bits, set_limit, k_hashes);
  } else {
    bf = new BloomFilter<std::array<int, MN_SIZE * MN_SIZE>>(m_bits, k_hashes);
  }
}

void DFS(MNPuzzle<MN_SIZE, MN_SIZE> &env,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &curr, int depth,
         int targetDepth, int upperBound,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &goal, BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *bf,
         BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *existingBf,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &parent) {
  // Pruning based on f-value
  double h = env.HCost(curr, goal);
  double f = depth + h;
  if (f > upperBound) {
    return;
  }

  if (depth == targetDepth) {
    if (existingBf == nullptr) {
      bf->add(curr.puzzle);
    } else {
      if (existingBf->maybe_contains(curr.puzzle)) {
        bf->add(curr.puzzle);
      }
    }
    return;
  }

  std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> neighbors;
  env.GetSuccessors(curr, neighbors);

  for (const auto &neighbor : neighbors) {
    if (neighbor == parent)
      continue;
    DFS(env, neighbor, depth + 1, targetDepth, upperBound, goal, bf, existingBf,
        curr);
  }
}

BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *GetBloomOfStatesInBloomAtDepth(
    MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal, MNPuzzle<MN_SIZE, MN_SIZE> env,
    int distance, BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *existingBf,
    int size_in_KiB, int k_hashes, BloomType type, double set_ratio) {
  
  size_t upperBound =
      distance * 2 + 1; // Distance cant be bigger then half of the cost
  BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *bf = nullptr;
  init_bloom_for_puzzle(bf, size_in_KiB, k_hashes, type, set_ratio);

  // If distance is 0, just add start
  if (distance == 0) {
    if (existingBf == nullptr) {
      bf->add(start.puzzle);
    } else {
      if (existingBf->maybe_contains(start.puzzle)) {
        bf->add(start.puzzle);
      }
    }
    return bf;
  }

  DFS(env, start, 0, distance, upperBound, goal, bf, existingBf, start);

  return bf;
}

using StateWithPath = std::pair<MNPuzzleState<MN_SIZE, MN_SIZE>, std::vector<slideDir>>;

static void CollectStatesInBloomAtExactDepth(
    MNPuzzle<MN_SIZE, MN_SIZE> &env,
    MNPuzzleState<MN_SIZE, MN_SIZE> curr,
    const MNPuzzleState<MN_SIZE, MN_SIZE> &goal,
    int depth,
    int targetDepth,
    int upperBound,
    const BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *bf,
    std::vector<slideDir> &movesSoFar,
    std::vector<StateWithPath> &out,
    slideDir lastMove)
{
  // f = g + h prune (same rationale you use elsewhere)
  const double h = env.HCost(curr, goal);
  const double f = depth + h;
  if (f > upperBound) return;

  if (depth == targetDepth) {
    if (bf && bf->maybe_contains(curr.puzzle)) {
      out.emplace_back(curr, movesSoFar); // vector copy is intentional
    }
    return;
  }

  std::vector<slideDir> moves;
  env.GetActions(curr, moves, lastMove);

  for (slideDir a : moves) {
    env.ApplyAction(curr, a);
    movesSoFar.push_back(a);

    CollectStatesInBloomAtExactDepth(env, curr, goal,
                                    depth + 1, targetDepth, upperBound,
                                    bf, movesSoFar, out, a);

    // restore
    slideDir inv = a;
    env.InvertAction(inv);
    env.ApplyAction(curr, inv);
    movesSoFar.pop_back();
  }
}

static bool SanityCheckBloomYieldsValidPath(
    const MNPuzzleState<MN_SIZE, MN_SIZE> &start,
    const MNPuzzleState<MN_SIZE, MN_SIZE> &goal,
    MNPuzzle<MN_SIZE, MN_SIZE> &env,
    int forwardDepth,
    int backwardDepth,
    const BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *bf,
    std::vector<slideDir> *outPath,   // optional; may be nullptr
    bool verbose = true)
{
  if (!bf || bf->get_n_inserted() == 0) {
    if (verbose) std::cerr << "[SANITY] Bloom is null/empty.\n";
    return false;
  }

  const int upperBound = forwardDepth + backwardDepth;

  // Collect candidates that are in the Bloom at the exact depths
  std::vector<StateWithPath> forwardStates, backwardStates;
  std::vector<slideDir> tmp;

  tmp.clear();
  CollectStatesInBloomAtExactDepth(env, start, goal,
                                  0, forwardDepth, upperBound,
                                  bf, tmp, forwardStates, kNoSlide);

  tmp.clear();
  CollectStatesInBloomAtExactDepth(env, goal, start,
                                  0, backwardDepth, upperBound,
                                  bf, tmp, backwardStates, kNoSlide);

  if (verbose) {
    std::cout << "[SANITY] forwardStates in-bloom @d=" << forwardDepth
              << ": " << forwardStates.size() << "\n";
    std::cout << "[SANITY] backwardStates in-bloom @d=" << backwardDepth
              << ": " << backwardStates.size() << "\n";
  }

  if (forwardStates.empty() || backwardStates.empty()) return false;

  // Hash forward meet-states -> path
  std::unordered_map<uint64_t, std::vector<slideDir>> fMap;
  fMap.reserve(forwardStates.size() * 2);

  for (const auto &p : forwardStates) {
    const uint64_t h = env.GetStateHash(p.first);
    // keep the first one; any is fine
    if (fMap.find(h) == fMap.end()) fMap.emplace(h, p.second);
  }

  // Try to meet + build a full path, then validate by simulation
  for (const auto &bp : backwardStates) {
    const uint64_t h = env.GetStateHash(bp.first);
    auto it = fMap.find(h);
    if (it == fMap.end()) continue;

    std::vector<slideDir> full = it->second;

    // Append backward path reversed with inverted actions (as you already do) :contentReference[oaicite:2]{index=2}
    for (auto rit = bp.second.rbegin(); rit != bp.second.rend(); ++rit) {
      slideDir inv = *rit;
      env.InvertAction(inv);
      full.push_back(inv);
    }

    // Validate: apply to start and ensure we reach goal
    MNPuzzleState<MN_SIZE, MN_SIZE> check = start;
    for (slideDir a : full) env.ApplyAction(check, a);

    if (check == goal) {
      if (verbose) {
        std::cout << "[SANITY] Found VALID path via bloom meet. len=" << full.size() << "\n";
      }
      if (outPath) *outPath = std::move(full);
      return true;
    }
  }

  if (verbose) {
    std::cerr << "[SANITY] No VALID meet found. Bloom may be false-positive-only or loop break was premature.\n";
  }
  return false;
}

std::vector<slideDir> solve_at_depth(MNPuzzleState<MN_SIZE, MN_SIZE> start,
                   MNPuzzleState<MN_SIZE, MN_SIZE> goal, MNPuzzle<MN_SIZE, MN_SIZE> env,
                   int forwardDepth,
                   int backwardDepth, int size_in_KiB, int k_hashes,
                   int minItemsInserted, BloomType bloomType, double set_ratio,
                   std::ostream *logFile, bool verbose) {
  BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *bf = nullptr;
  bool forward = true;
  int loopCount = 0;
  std::vector<int> insertedItems;
  std::string terminationReason = "min_items";
  std::ostringstream batchLog;

  std::deque<std::size_t> tail;

  static constexpr std::size_t ALT_PAIRS = 3;
  static constexpr std::size_t ALT_LEN   = ALT_PAIRS * 2;

  auto push_tail = [&](std::size_t v) {
    tail.push_back(v);
    if (tail.size() > ALT_LEN) tail.pop_front();
  };

  auto last_n_period2 = [&]() -> bool {
    if (tail.size() < ALT_LEN) return false;

    std::size_t a = tail[0];
    std::size_t b = tail[1];

    for (std::size_t i = 0; i < ALT_LEN; ++i) {
      std::size_t expected = (i % 2 == 0) ? a : b;
      if (tail[i] != expected) return false;
    }
    return true;
  };

  do {
    BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *nextBf;
    if (forward) {
      nextBf = GetBloomOfStatesInBloomAtDepth(start, goal, env, forwardDepth, bf,
                                              size_in_KiB, k_hashes, bloomType,
                                              set_ratio);
    } else {
      nextBf = GetBloomOfStatesInBloomAtDepth(goal, start, env, backwardDepth, bf,
                                              size_in_KiB, k_hashes, bloomType,
                                              set_ratio);
    }
    forward = !forward;

    if (bf) {
      delete bf;
    }
    bf = nextBf;

    insertedItems.push_back(bf->get_n_inserted());
    if (verbose) {
      batchLog << "Bloom filter populated with " << bf->get_n_inserted() << " items.\n";
    }

    loopCount++;
    if (loopCount > 200) {
      if (verbose) {
        batchLog << "Bloom filter loopCount: " << loopCount << ". Breaking loop.\n";
      }
      terminationReason = "loop_limit";
      break;
    }

    push_tail(bf->get_n_inserted());

    if (bf->get_n_inserted() > 0 && last_n_period2()) {
      if (verbose) {
        batchLog << "Bloom filter population stabilized. Breaking loop.\n";
      }
      terminationReason = "stabilized";
      break;
    }
    

  } while (bf->get_n_inserted() > (size_t)minItemsInserted);

  std::vector<slideDir> sanityPath;
  bool ok = SanityCheckBloomYieldsValidPath(start, goal, env,
                                            forwardDepth, backwardDepth,
                                            bf, &sanityPath,
                                            verbose);

  if (verbose && ok) {
    batchLog << "[SANITY] PASS for size=" << size_in_KiB << " KiB, k=" << k_hashes << "\n";
  }
  if (!ok){
    if (verbose) {
      batchLog << "[SANITY] FAIL for size=" << size_in_KiB << " KiB, k=" << k_hashes << "\n";
    }
    return std::vector<slideDir>();
  }

/*
  size_t numberOfPathsForward = 0;
  size_t numberOfPathsBackward = 0;

  if (terminationReason == "loop_limit") {
    
  }
*/
  if (logFile) {
    size_t set_limit = 0;
    size_t set_size = 0;
    if (bloomType == BloomType::WITH_SET) {
      set_limit = static_cast<size_t>((size_in_KiB * 1024 * 8ULL) * set_ratio /
                                      sizeof(MNPuzzleState<MN_SIZE, MN_SIZE>));
      if (set_limit == 0) {
        set_limit = 1;
      }
      auto *with_set =
          dynamic_cast<BloomFilterWithSet<MNPuzzle<MN_SIZE, MN_SIZE>,
                                          std::array<int, MN_SIZE * MN_SIZE>> *>(bf);
      if (with_set) {
        set_size = with_set->get_set_size();
      }
    }
    *logFile << size_in_KiB << "," << k_hashes << ",";
    if (bloomType == BloomType::WITH_SET) {
      *logFile << "with_set";
    } else {
      *logFile << "no_set";
    }
    *logFile << "," << set_ratio << "," << set_limit << "," << set_size << ",";
    *logFile << loopCount << "," << (bf ? bf->get_n_inserted() : 0) << ",";
    *logFile << terminationReason << ",";
    *logFile << "\"";
    for (size_t i = 0; i < insertedItems.size(); i++) {
      *logFile << insertedItems[i] << ",";
    }
    *logFile << "\"\n";
  }

  if (verbose) {
    std::cout << batchLog.str() << std::endl;
  }
  delete bf; // Clean up
  return sanityPath;
}

// Depth is temporary, later it will look for a path itterativley
int benchmark(MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal, MNPuzzle<MN_SIZE, MN_SIZE> env,
              int depth, int puzzle, std::ofstream &logFile, bool verbose) {
  int forwardDepth = depth / 2;
  int backwardDepth = depth - forwardDepth;

  int sizes[14] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
  int k_hashes[5] = {1, 2, 4, 8, 16};
  double set_ratios[] = {0.0, 0.01, 0.05, 0.10, 0.25};

  for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    for (int j = 0; j < sizeof(k_hashes) / sizeof(k_hashes[0]); j++) {
      for (double ratio : set_ratios) {
        BloomType bloomType = (ratio <= 0.0) ? BloomType::REGULAR : BloomType::WITH_SET;
        if (logFile.is_open()) {
          logFile << puzzle << ",";
        }
        solve_at_depth(start, goal, env, forwardDepth, backwardDepth, sizes[i],
                       k_hashes[j], 0, bloomType, ratio, &logFile, verbose);
      }
    }
  }
  return 0;
}

size_t countPathsToGoal(MNPuzzleState<MN_SIZE, MN_SIZE> curr,
                        MNPuzzleState<MN_SIZE, MN_SIZE> goal,
                        MNPuzzle<MN_SIZE, MN_SIZE> env,
                        int depth, int targetDepth, slideDir lastMove) {
  if (depth > targetDepth) {
    return 0;
  }
  if (depth == targetDepth && goal == curr) {
    return 1; // Found one path
  }
  size_t count = 0;
  std::vector<slideDir> moves;
  env.GetActions(curr, moves, lastMove); // Helper handles parent pruning
  for (slideDir a : moves) {
    env.ApplyAction(curr, a);
    count += countPathsToGoal(curr, goal, env, depth + 1, targetDepth, a);
    
    // restore state for next iteration
    slideDir inv = a;
    env.InvertAction(inv);
    env.ApplyAction(curr, inv);
  }
  return count;
}

void exploreSinglePuzzle(MNPuzzleState<MN_SIZE, MN_SIZE> start,
                         MNPuzzleState<MN_SIZE, MN_SIZE> goal,
                         MNPuzzle<MN_SIZE, MN_SIZE> env,
                         int depth, bool verbose) {
  size_t paths = countPathsToGoal(start, goal, env, 0, depth, kNoSlide);
  std::cout << "Paths to goal: " << paths;
}

std::vector<slideDir> solveBloom(MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal, MNPuzzle<MN_SIZE, MN_SIZE> env,
                                 int size_in_KiB, int k_hashes, BloomType bloomType, double set_ratio) {
  
  int minDistance = env.HCost(start, goal);

  int forwardDepth = minDistance / 2;
  int backwardDepth = minDistance - forwardDepth;

  while(true){
    std::vector<slideDir> path = solve_at_depth(start, goal, env, forwardDepth, backwardDepth, size_in_KiB, k_hashes, minDistance, bloomType, set_ratio, nullptr, false);

    if (path.size() > 0)
      return path;
    
    if (forwardDepth == backwardDepth) // up to this point we assume Cstar is fd + bd, if we reached here no solution was found, we increment bd first.
      backwardDepth++;
    else
      forwardDepth++; 
  }
}

std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>>
generateRandomState(int distance, int amount,
                    const MNPuzzleState<MN_SIZE, MN_SIZE> &start) {
  if (distance < 0)
    throw std::invalid_argument("distance must be non-negative");
  if (amount <= 0)
    throw std::invalid_argument("amount must be positive");

  MNPuzzle<MN_SIZE, MN_SIZE> mnp;

  struct Node {
    MNPuzzleState<MN_SIZE, MN_SIZE> state;
    int depth;
  };

  std::queue<Node> q;
  std::unordered_set<uint64_t> visited;
  std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> layerStates;

  q.push({start, 0});
  visited.insert(mnp.GetStateHash(start));

  std::vector<slideDir> acts;

  while (!q.empty()) {
    Node cur = q.front();
    q.pop();

    if (cur.depth == distance) {
      layerStates.push_back(cur.state);
      continue;
    }

    if (cur.depth > distance)
      continue;

    acts.clear();
    mnp.GetActions(cur.state, acts);
    for (slideDir a : acts) {
      MNPuzzleState<MN_SIZE, MN_SIZE> next = cur.state;
      mnp.ApplyAction(next, a);
      uint64_t h = mnp.GetStateHash(next);

      if (visited.insert(h).second) {
        q.push({next, cur.depth + 1});
      }
    }
  }

  if (static_cast<int>(layerStates.size()) < amount) {
    throw std::runtime_error("Not enough states at the requested distance.");
  }

  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(layerStates.begin(), layerStates.end(), gen);

  layerStates.resize(amount);
  return layerStates;
}

void printUsage(const char *progName) {
  std::cerr << "Usage:\n"
            << "  Generate: " << progName
            << " --generate -d <dist> -n <amount> -f <file>\n"
            << "  Benchmark: " << progName
            << " --benchmark -d <dist> -f <file>\n"
            << "  Optional:  " << progName << " --verbose\n";
}

struct STPResult {
  int instance;
  int solutionLength;
  double aStarTime;
  double revAStarTime;
  double baeTime;
  double mmTime;
  double idaTime;
  double revIdaTime;
  std::array<double, 3> bihsTime;

  size_t aStarNodeExpanded;
  size_t revAStarNodeExpanded;
  size_t baeNodeExpanded;
  size_t mmNodeExpanded;
  size_t idaNodeExpanded;
  size_t revIdaNodeExpanded;
  std::array<size_t, 3> bihsNodeExpanded;
};

static constexpr double TIMEOUT_SECONDS = 1000000000.0;
static constexpr int NUM_WORKERS = 1;

STPResult solveOneInstance(int i, std::ofstream &log, std::mutex &logMutex, std::ofstream &convLog, std::mutex &convMutex) {
  STPResult result;
  result.instance = i;
  result.solutionLength = -1;
  result.aStarTime = -1;
  result.revAStarTime = -1;
  result.baeTime = -1;
  result.mmTime = -1;
  result.idaTime = -1;
  result.revIdaTime = -1;
  result.bihsTime.fill(-1);
  result.bihsNodeExpanded.fill(0);
  result.aStarNodeExpanded = 0;
  result.revAStarNodeExpanded = 0;
  result.baeNodeExpanded = 0;
  result.mmNodeExpanded = 0;
  result.idaNodeExpanded = 0;
  result.revIdaNodeExpanded = 0;
  size_t minSize = 0;
  size_t frontierSize = 0;

  MNPuzzle<MN_SIZE, MN_SIZE> mnp;
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  goal.Reset();
  MNPuzzleState<MN_SIZE, MN_SIZE> puzzle = STP::GetKorfInstance(i);
  Timer t;

  std::cout << "[" << i << "] Running A*..." << std::flush;
  //A*
  {
    TemplateAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> astar;
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
    t.StartTimer();
    astar.GetPath(&mnp, puzzle, goal, path);
    t.EndTimer();
    result.aStarTime = t.GetElapsedTime();
    result.aStarNodeExpanded = astar.GetNodesExpanded();
    result.solutionLength = static_cast<int>(path.size()) - 1;

    minSize = astar.GetNumItems();
    std::cout << " done (" << result.aStarTime << "s, " << result.aStarNodeExpanded << "n)\n" << std::flush;
  }

  std::cout << "[" << i << "] Running Rev-A*..." << std::flush;
  // Reverse A*
  {
    TemplateAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> astar;
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
    t.StartTimer();
    astar.GetPath(&mnp, goal, puzzle, path);
    t.EndTimer();
    result.revAStarTime = t.GetElapsedTime();
    result.revAStarNodeExpanded = astar.GetNodesExpanded();
    result.solutionLength = static_cast<int>(path.size()) - 1;

    if (astar.GetNumItems() < minSize)
      minSize = astar.GetNumItems();
    std::cout << " done (" << result.revAStarTime << "s, " << result.revAStarNodeExpanded << "n)\n" << std::flush;
  }

  // If A* hit its node cap both ways the instance is too large — skip everything
  if (result.aStarNodeExpanded >= 10000000 && result.revAStarNodeExpanded >= 10000000) {
    std::cout << "[" << i << "] A* hit node cap both directions — skipping instance\n" << std::flush;
    return result;
  }

  // If either direction hit cap, BAE*/MM would likely OOM too — skip them and BiHS
  bool astarHitCap = (result.aStarNodeExpanded >= 10000000 && result.revAStarNodeExpanded >= 10000000);

  if (!astarHitCap) {
    std::cout << "[" << i << "] Running BAE*..." << std::flush;
    // BAE*
    {
      BAE<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> bae;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      t.StartTimer();
      bae.GetPath(&mnp, puzzle, goal, &mnp, &mnp, path);
      t.EndTimer();
      result.baeTime = t.GetElapsedTime();
      result.baeNodeExpanded = bae.GetNodesExpanded();
      result.solutionLength = static_cast<int>(path.size()) - 1;

      size_t baeSize = bae.GetNumForwardItems() + bae.GetNumBackwardItems();
      if (baeSize < minSize)
        minSize = baeSize;
      std::cout << " done (" << result.baeTime << "s, " << result.baeNodeExpanded << "n)\n" << std::flush;
    }

    std::cout << "[" << i << "] Running MM..." << std::flush;
    // MM
    {
      MM<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> mm;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      t.StartTimer();
      mm.GetPath(&mnp, puzzle, goal, &mnp, &mnp, path);
      t.EndTimer();
      result.mmTime = t.GetElapsedTime();
      result.mmNodeExpanded = mm.GetNodesExpanded();
      result.solutionLength = static_cast<int>(path.size()) - 1;

      size_t mmSize = mm.GetNumForwardItems() + mm.GetNumBackwardItems();
      if (mmSize < minSize)
        minSize = mmSize;
      frontierSize = mm.GetNumForwardItems();
      std::cout << " done (" << result.mmTime << "s, " << result.mmNodeExpanded << "n)\n" << std::flush;
    }
  }

  std::cout << "[" << i << "] Running IDA*..." << std::flush;
  // IDA*
  {
    IDAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, false> ida;
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
    t.StartTimer();
    ida.GetPath(&mnp, puzzle, goal, path);
    t.EndTimer();
    result.idaTime = t.GetElapsedTime();
    result.idaNodeExpanded = ida.GetNodesExpanded();
    result.solutionLength = static_cast<int>(path.size()) - 1;
    std::cout << " done (" << result.idaTime << "s, " << result.idaNodeExpanded << "n)\n" << std::flush;
  }

  std::cout << "[" << i << "] Running Rev-IDA*..." << std::flush;
  // Reverse IDA*
  {
    IDAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, false> ida;
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
    t.StartTimer();
    ida.GetPath(&mnp, goal, puzzle, path);
    t.EndTimer();
    result.revIdaTime = t.GetElapsedTime();
    result.revIdaNodeExpanded = ida.GetNodesExpanded();
    result.solutionLength = static_cast<int>(path.size()) - 1;
    std::cout << " done (" << result.revIdaTime << "s, " << result.revIdaNodeExpanded << "n)\n" << std::flush;
  }

  double maxBaselineTime = std::max({result.aStarTime, result.revAStarTime, result.baeTime,
                                     result.mmTime, result.idaTime, result.revIdaTime});
  double bihsTimeLimit = std::max(maxBaselineTime * 20.0, 120.0); // Set a minimum time limit of 120 seconds for BiHS-Bloom
  std::cout << "[" << i << "] BiHS-Bloom timeout limit: " << bihsTimeLimit << "s\n" << std::flush;

  double percentages[] = {0.5, 0.1, 0.01};
  // BiHS-Bloom
  if (astarHitCap) {
    std::cout << "[" << i << "] Skipping BiHS-Bloom (no frontierSize from MM)\n" << std::flush;
  }
  for(int pIdx = 0; !astarHitCap && pIdx < 3; ++pIdx)
  {
    double ratio = percentages[pIdx];
    int size_in_KiB = (minSize * get_state_size(puzzle) / 8192) * ratio ; // Convert bits to KiB

    // Let's cheat a little, i hav frontier size from MM so let's calculate optimal k by using opt_k = 9/13 * (m/n)

    int k_hashes = std::max(1, static_cast<int>(std::round((9.0 / 13.0) * (size_in_KiB * 8192.0 / (frontierSize * get_state_size(puzzle))))));

    // FP rate: (1 - e^(-k*n/m))^k  where n=frontier items, m=filter bits
    double fp_rate = std::pow(1.0 - std::exp(-(double)k_hashes * frontierSize / (size_in_KiB * 8192.0 / get_state_size(puzzle))), k_hashes);

    std::cout << "[" << i << "] Running BiHS-Bloom(" << (ratio*100) << "%, size=" << size_in_KiB
              << "KiB, k=" << k_hashes << ", fp_est=" << fp_rate
              << ", limit=" << bihsTimeLimit << "s)..." << std::flush;

    BiHSBloom<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> bihs(size_in_KiB, k_hashes, bihsTimeLimit);
    t.StartTimer();
    std::vector<slideDir> pathBiHS = bihs.GetPath(puzzle, goal);
    t.EndTimer();

    result.bihsTime[pIdx] = bihs.hasTimedOut() ? -1.0 : t.GetElapsedTime();
    result.bihsNodeExpanded[pIdx] = bihs.GetTotalNodesExpanded();
    bool converged = !bihs.hasTimedOut();

    if (!converged)
      std::cout << " TIMED OUT (" << result.bihsNodeExpanded[pIdx] << "n)\n" << std::flush;
    else
      std::cout << " done (" << result.bihsTime[pIdx] << "s, " << result.bihsNodeExpanded[pIdx] << "n)\n" << std::flush;

    {
      std::lock_guard<std::mutex> lk(logMutex);
      log << "BIHS_PARAM," << i << "," << ratio << "," << size_in_KiB << "," << k_hashes << ","
          << result.bihsTime[pIdx] << "," << result.bihsNodeExpanded[pIdx] << ","
          << fp_rate << "," << converged << "\n";
    }
    {
      std::lock_guard<std::mutex> lk(convMutex);
      for (const auto &s : bihs.GetIterStats())
        convLog << i << "," << size_in_KiB << "," << ratio << ","
                << s.totalDepth << "," << s.iteration << ","
                << s.nInserted << "," << s.nUnique << "," << s.estimatedFP << ","
                << s.bitsSet << "," << s.fillRatio << "," << s.expectedFillRatio << "\n";
      convLog.flush();
    }
    if (bihs.hasTimedOut()) break;
    result.solutionLength = static_cast<int>(pathBiHS.size());
  }

  return result;
}

void solveSTP(){
  std::ofstream log("benchmark_stp_korf100.csv");
  std::vector<std::string> headers = {"instance", "solution_length",
      "a_star_time", "rev_a_star_time", "bae_time", "mm_time", "ida_time", "rev_ida_time",
      "a_star_nodes", "rev_a_star_nodes", "bae_nodes", "mm_nodes", "ida_nodes", "rev_ida_nodes",
      "bihs_bloom_time_50pct", "bihs_bloom_time_10pct", "bihs_bloom_time_1pct",
      "bihs_bloom_nodes_50pct", "bihs_bloom_nodes_10pct", "bihs_bloom_nodes_1pct"};
  for(size_t i = 0; i < headers.size(); ++i) {
    log << headers[i];
    if (i < headers.size() - 1) log << ",";
  }
  log << "\n";

  std::ofstream convLog("bloom_convergence.csv");
  convLog << "instance,size_kib,ratio,total_depth,iteration,n_inserted,n_unique,estimated_fp,bits_set,fill_ratio,expected_fill_ratio\n";

  std::mutex logMutex;
  std::mutex convMutex;
  std::mutex coutMutex;
  std::atomic<int> nextInstance{0};
  int totalInstances = 100;

  auto worker = [&]() {
    while (true) {
      int i = nextInstance.fetch_add(1);
      if (i >= totalInstances) break;

      {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "Starting Korf's Puzzle #" << i << std::endl;
      }

      STPResult r = solveOneInstance(i, log, logMutex, convLog, convMutex);

      {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "Puzzle #" << r.instance
                  << " | A*: " << r.aStarTime << "s/" << r.aStarNodeExpanded << "n"
                  << " | Rev-A*: " << r.revAStarTime << "s/" << r.revAStarNodeExpanded << "n"
                  << " | BAE*: " << r.baeTime << "s/" << r.baeNodeExpanded << "n"
                  << " | MM: " << r.mmTime << "s/" << r.mmNodeExpanded << "n"
                  << " | IDA*: " << r.idaTime << "s/" << r.idaNodeExpanded << "n"
                  << " | Rev-IDA*: " << r.revIdaTime << "s/" << r.revIdaNodeExpanded << "n"
                  << " | BiHS-Bloom(50%): " << r.bihsTime[0] << "s/" << r.bihsNodeExpanded[0] << "n"
                  << " | BiHS-Bloom(10%): " << r.bihsTime[1] << "s/" << r.bihsNodeExpanded[1] << "n"
                  << " | BiHS-Bloom(1%): " << r.bihsTime[2] << "s/" << r.bihsNodeExpanded[2] << "n"
                  << " | Length: " << r.solutionLength
                  << std::endl;
      }

      {
        std::lock_guard<std::mutex> lk(logMutex);
        log << r.instance << "," << r.solutionLength << ","
            << r.aStarTime << ","
            << r.revAStarTime << ","
            << r.baeTime << ","
            << r.mmTime << ","
            << r.idaTime << ","
            << r.revIdaTime << ","
            << r.aStarNodeExpanded << "," << r.revAStarNodeExpanded << ","
            << r.baeNodeExpanded << "," << r.mmNodeExpanded << ","
            << r.idaNodeExpanded << "," << r.revIdaNodeExpanded << ","
            << r.bihsTime[0] << "," << r.bihsTime[1] << "," << r.bihsTime[2] << ","
            << r.bihsNodeExpanded[0] << "," << r.bihsNodeExpanded[1] << "," << r.bihsNodeExpanded[2] << "\n";
        log.flush();
      }
    }
  };

  std::vector<std::thread> threads;
  for (int t = 0; t < NUM_WORKERS; t++) {
    threads.emplace_back(worker);
  }
  for (auto &th : threads) {
    th.join();
  }

  log.close();
  std::cout << "Results written to benchmark_stp_korf100.csv" << std::endl;
}

void solvePancake(){
  PancakePuzzle<16> mnp;
  PancakePuzzleState<16> goal;
  goal.Reset();
  IDAStar<PancakePuzzleState<16>, PancakePuzzleAction, false> ida;
  PancakePuzzleState<16> puzzle;
  std::vector<PancakePuzzleState<16>> pathIDA;
  Timer t;
  std::vector<PancakePuzzleAction> pathBiHS;
  int size_in_KiB = 8000;
  int k_hashes = 4;
  BiHSBloom<PancakePuzzleState<16>, PancakePuzzleAction, PancakePuzzle<16>> bihs(size_in_KiB, k_hashes);

  std::ofstream log("benchmark_pancake16_100.csv");
  log << "instance,solution_length,ida_time,bihs_bloom_time\n";

  for (int i = 0; i < 100; i++) {
    std::cout << "Solving Pancake challenge #" << i << std::endl;

    GetPancakeInstance(puzzle, i);

    t.StartTimer();
    ida.GetPath(&mnp, puzzle, goal, pathIDA);
    t.EndTimer();
    double idaTime = t.GetElapsedTime();

    std::cout << "IDAStar Solve time: " << idaTime << std::endl;

    t.StartTimer();
    pathBiHS = bihs.GetPath(puzzle, goal);
    t.EndTimer();
    double bihsTime = t.GetElapsedTime();

    std::cout << "BIHS Bloom Solve time: " << bihsTime << std::endl;

    log << i << "," << pathBiHS.size() << "," << idaTime << "," << bihsTime << "\n";
    log.flush();

    pathIDA.clear();
    pathBiHS.clear();
  }
  log.close();
  std::cout << "Results written to benchmark_pancake16_100.csv" << std::endl;
}


#ifndef STP_BIHS_BLOOM_TEST
int main(int argc, char **argv) {
  bool slidingTilePuzzle = false;
  bool pancake = false;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--stp") == 0 )
      slidingTilePuzzle = true;
    else if (strcmp(argv[i], "--pancake") == 0)
      pancake = true;
  }

  
  if (!(pancake ^ slidingTilePuzzle)){
    std::cerr << "Please choose exactly 1 domain";
    return 0;
  }

  if (slidingTilePuzzle) {
    std::cout << "Domain: Sliding Tile Puzzle" << std::endl;
    solveSTP();
  }
  else if (pancake) {
    std::cout << "Domain: Pancake Puzzle" << std::endl;
    solvePancake();
  }
} 
#endif

int main_old(int argc, char **argv) {
  bool generate = false;
  bool benchmarkMode = false;
  bool solveMode = false;
  int distance = -1;
  int amount = -1;
  std::string filename = "";
  bool debug = false;
  bool verbose = false;
  bool explore = false;
  int puzzle = -1; 

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--generate") == 0 || strcmp(argv[i], "-g") == 0) {
      generate = true;
    } else if (strcmp(argv[i], "--benchmark") == 0 ||
               strcmp(argv[i], "-b") == 0) {
      benchmarkMode = true;
    } else if (strcmp(argv[i], "--distance") == 0 ||
               strcmp(argv[i], "-d") == 0) {
      if (i + 1 < argc)
        distance = std::atoi(argv[++i]);
    } else if (strcmp(argv[i], "--amount") == 0 || strcmp(argv[i], "-n") == 0) {
      if (i + 1 < argc)
        amount = std::atoi(argv[++i]);
    } else if (strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) {
      if (i + 1 < argc)
        filename = argv[++i];
    } else if (strcmp(argv[i], "--debug") == 0) {
      debug = true;
    } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
      verbose = true;
    } else if (strcmp(argv[i], "--solve") == 0 ||
               strcmp(argv[i], "-s") == 0) {
      solveMode = true;
    } else if (strcmp(argv[i], "--explore") == 0 ||
               strcmp(argv[i], "-e") == 0) {
      explore = true;
    } else if (strcmp(argv[i], "--puzzle") == 0 ||
               strcmp(argv[i], "-p") == 0) {
      if (i + 1 < argc)
        puzzle = std::atoi(argv[++i]);
    }
  }

  bool modes[] = {generate, benchmarkMode, solveMode, explore};
  int modeCount = 0;
  for (bool mode : modes) {
    if (mode) {
      modeCount++;
    }
  }

  if (modeCount != 1) {
    std::cerr
        << "Error: Must specify exactly one of --generate or --benchmark or --solve.\n";
    printUsage(argv[0]);
    return 1;
  }

  if (filename.empty()) {
    std::cout << "No file was specified, using KORF's puzzles.\n";
  }

  if (generate) {
    if (distance < 0 || amount <= 0) {
      std::cerr
          << "Error: For generation, distance must be >= 0 and amount > 0.\n";
      return 1;
    }

    MNPuzzleState<MN_SIZE, MN_SIZE> start;
    start.Reset();

    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> states;
    try {
      Timer t;
      t.StartTimer();
      states = generateRandomState(distance, amount, start);
      t.EndTimer();
      if (verbose) {
        std::cout << "Generation took " << t.GetElapsedTime() << " seconds\n";
      }
    } catch (const std::exception &e) {
      std::cerr << "Generation failed: " << e.what() << "\n";
      return 1;
    }

    std::ofstream out(filename);
    if (!out) {
      std::cerr << "Error: Cannot open output file: " << filename << "\n";
      return 1;
    }

    for (const auto &s : states) {
      writeStateFlat<MN_SIZE>(out, s);
    }

    out.close();

    if (verbose) {
      std::cout << "Successfully generated " << states.size()
                << " puzzle states at distance " << distance << " and saved to "
                << filename << "\n";
    }
  } else if (explore) {
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> puzzles;
    MNPuzzle<MN_SIZE, MN_SIZE>::read_in_mn_puzzles(filename.c_str(), false,
                                                   10000, puzzles);

    if (puzzles.empty()) {
      std::cerr << "No puzzles loaded from " << filename << "\n";
      return 1;
    }

    MNPuzzleState<MN_SIZE, MN_SIZE> goal;
    goal.Reset();

    std::cout << "Puzzle " << puzzle << ":\n";
    std::cout << puzzles[puzzle] << "\n";

    MNPuzzle<MN_SIZE, MN_SIZE> env;

    exploreSinglePuzzle(puzzles[puzzle], goal, env, distance, verbose);
    return 1;
  } 
  else {
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> puzzles;
    if (filename.empty()) {
      for (int i = 0; i < 100; i++) {
        puzzles.push_back(STP::GetKorfInstance(i));
      }
    } else {
      MNPuzzle<MN_SIZE, MN_SIZE>::read_in_mn_puzzles(filename.c_str(), false,
                                                     10000, puzzles);

      if (puzzles.empty()) {
        std::cerr << "No puzzles loaded from " << filename << "\n";
        return 1;
      }
    }

    MNPuzzleState<MN_SIZE, MN_SIZE> goal;
    goal.Reset();

    if (verbose) {
      std::cout << "Loaded " << puzzles.size() << " puzzles. Solving...\n";
    }
    std::ofstream logFile("bloom_stats.csv");
    if (logFile && benchmarkMode) {
      logFile << "Puzzle,Size_KiB,K_Hashes,Mode,Set_Ratio,Set_Limit,Set_Size,Loop_Count,Final_Inserted,Termination,Inserted\n";
    }
    else if (logFile && solveMode) {
      logFile << "Puzzle,A_Star_Time,IDAStar_Time,Bloom_Time,New_BiHS_Time\n";
    }
    else {
      std::cerr << "Error: Cannot open log file\n";
      return 1;
    }

    for (size_t i = 0; i < puzzles.size(); ++i) {
      if (verbose) {
        std::cout << "Puzzle " << i << ": ";
      }
      // Sanity check, check size by running A star
      MNPuzzle<MN_SIZE, MN_SIZE> mnp;
      TemplateAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir,
                    MNPuzzle<MN_SIZE, MN_SIZE>>
          astar;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      
      Timer t;
      t.StartTimer();
      astar.GetPath(&mnp, puzzles[i], goal, path);
      t.EndTimer();
      double aStarTime = t.GetElapsedTime();

      if (verbose) {
        std::cout << "A* Path found length: " << (path.size() - 1) << '\n'; // -1 because we don't count the start state
        std::cout << "A* Nodes expanded: " << astar.GetNodesExpanded() << '\n';
        std::cout << "A* Time: " << aStarTime << '\n';
      }
      
      // print steps if debug
      if (debug) {
        for (const auto &s : path) {
          std::cout << s << std::endl;
        }
      }
      if (benchmarkMode) {
        benchmark(puzzles[i], goal, mnp, distance, i, logFile, verbose);
      } else {

        IDAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, false> ida;
	      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> pathIDA;
        
        t.StartTimer();
        ida.GetPath(&mnp, puzzles[i], goal, pathIDA);
        t.EndTimer();
        double idaTime = t.GetElapsedTime();

        if (verbose) {
          std::cout << "------------------------------" << std::endl;
          std::cout << "IDA* Path found length: " << (pathIDA.size() - 1) << '\n'; // -1 because we don't count the start state
          std::cout << "IDA* Nodes expanded: " << ida.GetNodesExpanded() << '\n';
          std::cout << "IDA* Time: " << idaTime << '\n';
          std::cout << "------------------------------" << std::endl;
          //std::cout << "BiHS-Bloom Solving...\n";
        }

        //TODO: Get Actual Values
        int size_in_KiB = 4000;
        int k_hashes = 2;
        BloomType bloomType = BloomType::REGULAR;
        double set_ratio = 0.0;
        std::vector<slideDir> pathBloom;
        
        t.StartTimer();
        // pathBloom = solveBloom(puzzles[i], goal, mnp, size_in_KiB, k_hashes, bloomType, set_ratio);
        t.EndTimer();
        double bloomTime = t.GetElapsedTime();

        /**
        if (verbose) {
          std::cout << "BiHS-Bloom Time: " << bloomTime << '\n';
          std::cout << "BiHS-Bloom Path found length: " << pathBloom.size() << '\n';
        }
        

        if (pathBloom.size() == 0){
          std::cout << "BiHS-Bloom Path not found\n";
        }
        else{
          //Sanity check
          MNPuzzleState<MN_SIZE, MN_SIZE> check = puzzles[i];
          for (slideDir a : pathBloom) mnp.ApplyAction(check, a);
          if (check == goal) {
            if (verbose) {
              std::cout << "Passed sanity check\n";
              std::cout << "------------------------------" << std::endl;
            }
          }
          else{
            std::cout << "Failed sanity check\n";
            std::cout << "------------------------------" << std::endl;
            return 0;
          }
        }
        */

        if (verbose) {
          std::cout << "------------------------------" << std::endl;
          std::cout << "Testing BiHS\n";
        }

        std::vector<slideDir> pathBiHS;
        BiHSBloom<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> bihs(size_in_KiB, k_hashes);

        t.StartTimer();
        pathBiHS = bihs.GetPath(puzzles[i], goal);
        t.EndTimer();
        double bihsTime = t.GetElapsedTime();

        if (verbose) {
          std::cout << "BiHS Time: " << bihsTime << '\n';
          std::cout << "BiHS Path found length: " << pathBiHS.size() << std::endl;
        }

        t.StartTimer();
        pathBiHS = bihs.GetPath(puzzles[i], goal, true);
        t.EndTimer();
        double bihsTimeRecursive = t.GetElapsedTime();

        if (verbose) {
          std::cout << "Recursive BiHS Time: " << bihsTime << '\n';
          std::cout << "Recursive BiHS Path found length: " << pathBiHS.size() << '\n';
        }

        logFile << i << "," << aStarTime << "," << idaTime << "," << bloomTime << "," << bihsTime << "\n";
      }
    }
    if (logFile)
      logFile.close();
  }

  return 0;
}
