#include "Driver.h"
#include "Bloom.h"

#include "IncrementalIDA.h"
#include "MNPuzzle.h"
#include "TemplateAStar.h"
#include "Timer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

void init_bloom_for_puzzle(BloomFilter *bf, int size_in_KiB, int k_hashes) {
  size_t m_bits = size_in_KiB * 1024 * 8ULL;

  if (bloom_init_mk(bf, m_bits, k_hashes) != 0) {
    std::cerr << "Failed to init bloom filter\n";
    std::abort();
  }
}

void DFS(MNPuzzle<MN_SIZE, MN_SIZE> &env,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &curr, int depth,
         int targetDepth, int upperBound,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &goal, BloomFilter *bf,
         BloomFilter *existingBf,
         const MNPuzzleState<MN_SIZE, MN_SIZE> &parent) {
  // Pruning based on f-value
  double h = env.HCost(curr, goal);
  double f = depth + h;
  if (f > upperBound) {
    return;
  }

  if (depth == targetDepth) {
    if (existingBf == nullptr) {
      bloom_add(bf, &curr.puzzle, sizeof(curr.puzzle));
    } else {
      if (bloom_maybe_contains(existingBf, &curr.puzzle, sizeof(curr.puzzle))) {
        bloom_add(bf, &curr.puzzle, sizeof(curr.puzzle));
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

BloomFilter *GetBloomOfStatesInBloomAtDepth(
    MNPuzzleState<MN_SIZE, MN_SIZE> start, MNPuzzleState<MN_SIZE, MN_SIZE> goal,
    int distance, BloomFilter *existingBf, int size_in_KiB, int k_hashes) {
  MNPuzzle<MN_SIZE, MN_SIZE> env;
  size_t upperBound =
      distance * 2 + 1; // Distance cant be bigger then half of the cost
  BloomFilter *bf = new BloomFilter();
  init_bloom_for_puzzle(bf, size_in_KiB, k_hashes);

  // If distance is 0, just add start
  if (distance == 0) {
    if (existingBf == nullptr) {
      bloom_add(bf, &start.puzzle, sizeof(start.puzzle));
    } else {
      if (bloom_maybe_contains(existingBf, &start.puzzle,
                               sizeof(start.puzzle))) {
        bloom_add(bf, &start.puzzle, sizeof(start.puzzle));
      }
    }
    return bf;
  }

  DFS(env, start, 0, distance, upperBound, goal, bf, existingBf, start);

  return bf;
}

int solve_at_depth(MNPuzzleState<MN_SIZE, MN_SIZE> start,
                   MNPuzzleState<MN_SIZE, MN_SIZE> goal, int forwardDepth,
                   int backwardDepth, int size_in_KiB, int k_hashes,
                   int minItemsInserted, std::ostream *logFile) {
  BloomFilter *bf = nullptr;
  bool forward = true;
  int stabilizedLoopCount = 0;
  int loopCount = 0;
  std::vector<int> insertedItems;

  do {
    BloomFilter *nextBf;
    if (forward) {
      nextBf = GetBloomOfStatesInBloomAtDepth(start, goal, forwardDepth, bf,
                                              size_in_KiB, k_hashes);
    } else {
      nextBf = GetBloomOfStatesInBloomAtDepth(goal, start, backwardDepth, bf,
                                              size_in_KiB, k_hashes);
    }
    forward = !forward;

    if (bf) {
      bloom_free(bf);
      delete bf;
    }
    bf = nextBf;

    insertedItems.push_back(bf->n_inserted);
    std::cout << "Bloom filter populated with " << bf->n_inserted << " items."
              << std::endl;

    // Safety break to prevent infinite loop if count doesn't increase (since
    // depth is constant)
    // TODo: limit number of loops

    loopCount++;
    if (loopCount > 100) {
      std::cout << "Bloom filter loopCount: " << loopCount
                << ". Breaking loop.\n";
      break;
    }

    static size_t last_inserted = 0;
    if (bf->n_inserted == last_inserted && bf->n_inserted > 0) {
      stabilizedLoopCount++;
      if (stabilizedLoopCount > 10) {
        std::cout << "Bloom filter population stabilized. Breaking loop.\n";
        break;
      }
    }
    last_inserted = bf->n_inserted;

  } while (bf->n_inserted > minItemsInserted);

  if (logFile) {
    *logFile << size_in_KiB << "," << k_hashes << ",";
    *logFile << "[";
    for (size_t i = 0; i < insertedItems.size(); i++) {
      *logFile << insertedItems[i] << ",";
    }
    *logFile << "]\n";
  }

  std::cout << "\n" << std::endl;
  bloom_free(bf); // Clean up
  delete bf;
  return 0;
}

// Depth is temporary, later it will look for a path itterativley
int benchmark(MNPuzzleState<MN_SIZE, MN_SIZE> start,
              MNPuzzleState<MN_SIZE, MN_SIZE> goal, int depth, int puzzle,
              std::ofstream &logFile) {
  int forwardDepth = depth / 2;
  int backwardDepth = depth - forwardDepth;

  int sizes[14] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
  int k_hashes[5] = {1, 2, 4, 8, 16};

  for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    for (int j = 0; j < sizeof(k_hashes) / sizeof(k_hashes[0]); j++) {
      if (logFile.is_open())
        logFile << puzzle << ",";
      solve_at_depth(start, goal, forwardDepth, backwardDepth, sizes[i],
                     k_hashes[j], 3, &logFile);
    }
  }
  return 0;
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
            << " --benchmark -d <dist> -f <file>\n";
}

int main(int argc, char **argv) {
  bool generate = false;
  bool benchmarkMode = false;
  int distance = -1;
  int amount = -1;
  std::string filename;
  bool debug = false;

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
    }
  }

  if ((generate && benchmarkMode) || (!generate && !benchmarkMode)) {
    std::cerr
        << "Error: Must specify exactly one of --generate or --benchmark.\n";
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
      std::cout << "Generation took " << t.GetElapsedTime() << " seconds\n";
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

    std::cout << "Successfully generated " << states.size()
              << " puzzle states at distance " << distance << " and saved to "
              << filename << "\n";
  } else {
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> puzzles;
    MNPuzzle<MN_SIZE, MN_SIZE>::read_in_mn_puzzles(filename.c_str(), false,
                                                   10000, puzzles);

    if (puzzles.empty()) {
      std::cerr << "No puzzles loaded from " << filename << "\n";
      return 1;
    }

    MNPuzzleState<MN_SIZE, MN_SIZE> goal;
    goal.Reset();

    std::cout << "Loaded " << puzzles.size() << " puzzles. Solving...\n";

    std::ofstream logFile("bloom_stats.csv");
    if (logFile) {
      logFile << "Puzzle,Size_KiB,K_Hashes,Inserted\n";
    }

    for (size_t i = 0; i < puzzles.size(); ++i) {
      std::cout << "Puzzle " << i << ": ";
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

      std::cout << "A* Path found length: " << path.size() << std::endl;
      std::cout << "A* Nodes expanded: " << astar.GetNodesExpanded()
                << std::endl;
      std::cout << "A* Time: " << t.GetElapsedTime() << std::endl;
      // print steps if debug
      if (debug) {
        for (const auto &s : path) {
          std::cout << s << std::endl;
        }
      }

      benchmark(puzzles[i], goal, distance, i, logFile);
    }
    if (logFile)
      logFile.close();
  }

  return 0;
}
