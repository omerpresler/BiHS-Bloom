#include "Driver.h"
#include "Bloom.hpp"

#include "IncrementalIDA.h"
#include "MNPuzzle.h"
#include "TemplateAStar.h"
#include "Timer.h"

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
    MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal,
    int distance, BloomFilter<std::array<int, MN_SIZE * MN_SIZE>> *existingBf,
    int size_in_KiB, int k_hashes, BloomType type, double set_ratio) {
  MNPuzzle<MN_SIZE, MN_SIZE> env;
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

  MNPuzzle<MN_SIZE, MN_SIZE> env;
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

int solve_at_depth(MNPuzzleState<MN_SIZE, MN_SIZE> start,
                   MNPuzzleState<MN_SIZE, MN_SIZE> goal, int forwardDepth,
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
      nextBf = GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, bf,
                                              size_in_KiB, k_hashes, bloomType,
                                              set_ratio);
    } else {
      nextBf = GetBloomOfStatesInBloomAtDepth(goal, start, backwardDepth, bf,
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
  bool ok = SanityCheckBloomYieldsValidPath(start, goal,
                                            forwardDepth, backwardDepth,
                                            bf, &sanityPath,
                                            verbose);
  if (!ok) {
    throw std::runtime_error("Sanity failed; benchmark invalid");
  }

  if (verbose) {
    batchLog << "[SANITY] PASS for size=" << size_in_KiB << " KiB, k=" << k_hashes << "\n";
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
  return 0;
}

// Depth is temporary, later it will look for a path itterativley
int benchmark(MNPuzzleState<MN_SIZE, MN_SIZE> start,
              MNPuzzleState<MN_SIZE, MN_SIZE> goal, int depth, int puzzle,
              std::ofstream &logFile, bool verbose) {
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
        solve_at_depth(start, goal, forwardDepth, backwardDepth, sizes[i],
                       k_hashes[j], 0, bloomType, ratio, &logFile, verbose);
      }
    }
  }
  return 0;
}

void exploreSinglePuzzle(MNPuzzleState<MN_SIZE, MN_SIZE> start,
                         MNPuzzleState<MN_SIZE, MN_SIZE> goal, bool verbose) {
 
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

int main(int argc, char **argv) {
  bool generate = false;
  bool benchmarkMode = false;
  bool solveMode = false;
  int distance = -1;
  int amount = -1;
  std::string filename;
  bool debug = false;
  bool verbose = false;
  bool exploreSinglePuzzle = false;
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
      exploreSinglePuzzle = true;
    } else if (strcmp(argv[i], "--puzzle") == 0 ||
               strcmp(argv[i], "-p") == 0) {
      if (i + 1 < argc)
        puzzle = std::atoi(argv[++i]);
    }
  }

  if ((generate && benchmarkMode) || (generate && solveMode) || (benchmarkMode && solveMode) || (!generate && !benchmarkMode && !solveMode)) {
    std::cerr
        << "Error: Must specify exactly one of --generate or --benchmark or --solve.\n";
    printUsage(argv[0]);
    return 1;
  }

  if (filename.empty()) {
    std::cerr << "Error: Must specify filename with -f.\n";
    printUsage(argv[0]);
    return 1;
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
  } else if (exploreSinglePuzzle) {
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> puzzles;
    MNPuzzle<MN_SIZE, MN_SIZE>::read_in_mn_puzzles(filename.c_str(), false,
                                                   10000, puzzles);

    if (puzzles.empty()) {
      std::cerr << "No puzzles loaded from " << filename << "\n";
      return 1;
    }

    MNPuzzleState<MN_SIZE, MN_SIZE> goal;
    goal.Reset();

    exploreSinglePuzzle(puzzles[puzzle], goal, verbose);
  } 
  else {
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> puzzles;
    MNPuzzle<MN_SIZE, MN_SIZE>::read_in_mn_puzzles(filename.c_str(), false,
                                                   10000, puzzles);

    if (puzzles.empty()) {
      std::cerr << "No puzzles loaded from " << filename << "\n";
      return 1;
    }

    MNPuzzleState<MN_SIZE, MN_SIZE> goal;
    goal.Reset();

    if (verbose) {
      std::cout << "Loaded " << puzzles.size() << " puzzles. Solving...\n";
    }

    std::ofstream logFile("bloom_stats.csv");
    if (logFile) {
      logFile << "Puzzle,Size_KiB,K_Hashes,Mode,Set_Ratio,Set_Limit,Set_Size,Loop_Count,Final_Inserted,Termination,Inserted\n";
    }
    else {
      std::cerr << "Error: Cannot open log file\n";
      return 1;
    }

    for (size_t i = 0; i < puzzles.size(); ++i) {
      std::ostringstream batchLog;
      if (verbose) {
        batchLog << "Puzzle " << i << ": ";
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

      if (verbose) {
        batchLog << "A* Path found length: " << path.size() << '\n';
        batchLog << "A* Nodes expanded: " << astar.GetNodesExpanded() << '\n';
        batchLog << "A* Time: " << t.GetElapsedTime() << '\n';
      }
      // print steps if debug
      if (debug) {
        for (const auto &s : path) {
          std::cout << s << std::endl;
        }
      }
      if (benchmarkMode) {
        benchmark(puzzles[i], goal, distance, i, logFile, verbose);
      } else {
        Timer t;
        std::vector<slideDir> path;
        if (verbose) {
          batchLog << "BiHS-Bloom Solving...\n";
        }
        t.StartTimer();
        path = solveBloom(puzzles[i], goal);
        t.EndTimer();
        if (verbose) {
          batchLog << "BiHS-Bloom Time: " << t.GetElapsedTime() << '\n';
          batchLog << "BiHS-Bloom Path found length: " << path.size() << '\n';
        }
      }
      if (verbose) {
        std::cout << batchLog.str();
      }
    }
    if (logFile)
      logFile.close();
  }

  return 0;
}
