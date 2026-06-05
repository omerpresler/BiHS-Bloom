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
#include "NBS.h"

#include "PancakeInstances.h"

#include <algorithm>
#include <cmath>
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


struct STPResult {
  int instance;
  int solutionLength;
  double aStarTime;
  double revAStarTime;
  double baeTime;
  double mmTime;
  double nbsTime;
  double idaTime;
  double revIdaTime;
  std::array<double, 3> bihsTime;

  size_t aStarNodeExpanded;
  size_t revAStarNodeExpanded;
  size_t baeNodeExpanded;
  size_t mmNodeExpanded;
  size_t nbsNodeExpanded;
  size_t idaNodeExpanded;
  size_t revIdaNodeExpanded;
  std::array<size_t, 3> bihsNodeExpanded;
};

static constexpr int NUM_WORKERS = 1;

STPResult solveOneInstance(int i, std::ofstream &log, std::mutex &logMutex, std::ofstream &convLog, std::mutex &convMutex) {
  STPResult result;
  result.instance = i;
  result.solutionLength = -1;
  result.aStarTime = -1;
  result.revAStarTime = -1;
  result.baeTime = -1;
  result.mmTime = -1;
  result.nbsTime = -1;
  result.idaTime = -1;
  result.revIdaTime = -1;
  result.bihsTime.fill(-1);
  result.bihsNodeExpanded.fill(0);
  result.aStarNodeExpanded = 0;
  result.revAStarNodeExpanded = 0;
  result.baeNodeExpanded = 0;
  result.mmNodeExpanded = 0;
  result.nbsNodeExpanded = 0;
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
    try {
      t.StartTimer();
      astar.GetPath(&mnp, puzzle, goal, path);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      printf("A* ran out of memory\n");
    }
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
    try {
      t.StartTimer();
      astar.GetPath(&mnp, goal, puzzle, path);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      printf("Rev-A* ran out of memory\n");
    }
    result.revAStarTime = t.GetElapsedTime();
    result.revAStarNodeExpanded = astar.GetNodesExpanded();
    result.solutionLength = static_cast<int>(path.size()) - 1;

    if (astar.GetNumItems() < minSize)
      minSize = astar.GetNumItems();
    std::cout << " done (" << result.revAStarTime << "s, " << result.revAStarNodeExpanded << "n)\n" << std::flush;
  }


  {
    std::cout << "[" << i << "] Running BAE*..." << std::flush;
    // BAE*
    {
      BAE<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> bae;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      try {
        t.StartTimer();
        bae.GetPath(&mnp, puzzle, goal, &mnp, &mnp, path);
        t.EndTimer();
      }
      catch (const std::bad_alloc&) {
        t.EndTimer();
        printf("BAE* ran out of memory\n");
      }
      result.baeTime = t.GetElapsedTime();
      result.baeNodeExpanded = bae.GetNodesExpanded();
      result.solutionLength = static_cast<int>(path.size()) - 1;

      size_t baeSize = bae.GetNumForwardItems() + bae.GetNumBackwardItems();
      //if (baeSize < minSize)
      //  minSize = baeSize;
      std::cout << " done (" << result.baeTime << "s, " << result.baeNodeExpanded << "n)\n" << std::flush;
    }

    std::cout << "[" << i << "] Running NBS..." << std::flush;
    // NBS
    {
      NBS<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> nbs;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      try {
        t.StartTimer();
        nbs.GetPath(&mnp, puzzle, goal, &mnp, &mnp, path);
        t.EndTimer();
      }
      catch (const std::bad_alloc&) {
          t.EndTimer();
          printf("NBS ran out of memory\n");
      }
     
      result.nbsTime = t.GetElapsedTime();
      result.nbsNodeExpanded = nbs.GetNodesExpanded();
      result.solutionLength = static_cast<int>(path.size()) - 1;

      std::cout << " done (" << result.nbsTime << "s, " << result.nbsNodeExpanded << "n)\n" << std::flush;
    }

    std::cout << "[" << i << "] Running MM..." << std::flush;
    // MM
    {
      MM<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> mm;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      try {
        t.StartTimer();
        mm.GetPath(&mnp, puzzle, goal, &mnp, &mnp, path);
        t.EndTimer();
      }
      catch (const std::bad_alloc&) {
        t.EndTimer();
        printf("MM ran out of memory\n");
      }
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
    try {
      t.StartTimer();
      ida.GetPath(&mnp, puzzle, goal, path);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      printf("IDA* ran out of memory\n");
    }
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
    try {
      t.StartTimer();
      ida.GetPath(&mnp, goal, puzzle, path);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      printf("Rev-IDA* ran out of memory\n");
    }
    result.revIdaTime = t.GetElapsedTime();
    result.revIdaNodeExpanded = ida.GetNodesExpanded();
    result.solutionLength = static_cast<int>(path.size()) - 1;
    std::cout << " done (" << result.revIdaTime << "s, " << result.revIdaNodeExpanded << "n)\n" << std::flush;
  }

  double maxBaselineTime = std::max({result.aStarTime, result.revAStarTime, result.baeTime, result.nbsTime,
                                     result.mmTime, result.idaTime, result.revIdaTime});
  double bihsTimeLimit = std::max(maxBaselineTime * 20.0, 120.0); // Set a minimum time limit of 120 seconds for BiHS-Bloom
  std::cout << "[" << i << "] BiHS-Bloom timeout limit: " << bihsTimeLimit << "s\n" << std::flush;

  double percentages[] = {0.5, 0.1, 0.01};
  // BiHS-Bloom
  for(int pIdx = 0; pIdx < 3; ++pIdx)
  {
    double ratio = percentages[pIdx];
    int size_in_KiB = std::max(1, static_cast<int>(
        std::round((static_cast<double>(minSize) * get_state_size(puzzle) / 8192.0) * ratio))); // Convert bits to KiB

    // Estimate the best Bloom hash count as ln(2) * (m / n).
    double bloomBits = size_in_KiB * 8192.0;
    double estimatedFrontierItems = std::max(1.0, static_cast<double>(frontierSize));
    int k_hashes = std::max(1, static_cast<int>(
        std::round(std::log(2.0) * bloomBits / estimatedFrontierItems)));

    // FP rate: (1 - e^(-k*n/m))^k where n=estimated items, m=Bloom bits.
    double fp_rate = std::pow(1.0 - std::exp(-(double)k_hashes * estimatedFrontierItems / bloomBits), k_hashes);

    std::cout << "[" << i << "] Running BiHS-Bloom(" << (ratio*100) << "%, size=" << size_in_KiB
              << "KiB, k=" << k_hashes << ", fp_est=" << fp_rate
              << ", limit=" << bihsTimeLimit << "s)..." << std::flush;

    BiHSBloom<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> bihs(size_in_KiB, k_hashes, bihsTimeLimit);
    bool bihsOutOfMemory = false;
    std::vector<slideDir> pathBiHS;
    try {
      t.StartTimer();
      pathBiHS = bihs.GetPath(puzzle, goal);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      bihsOutOfMemory = true;
      printf("BiHS-Bloom ran out of memory\n");
    }

    result.bihsTime[pIdx] = bihsOutOfMemory ? -2.0 : (bihs.hasTimedOut() ? -1.0 : t.GetElapsedTime());
    result.bihsNodeExpanded[pIdx] = bihs.GetTotalNodesExpanded();
    bool converged = !bihsOutOfMemory && !bihs.hasTimedOut();

    if (bihsOutOfMemory)
      std::cout << " OUT OF MEMORY (" << result.bihsNodeExpanded[pIdx] << "n)\n" << std::flush;
    else if (!converged)
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
                << s.bitsSet << "," << s.fillRatio << "," << s.expectedFillRatio << ","
                << s.materializedForwardStates << "," << s.materializedBackwardStates << ","
                << s.materializedTotalStates << "\n";
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
      "a_star_time", "rev_a_star_time", "bae_time", "nbs_time", "mm_time", "ida_time", "rev_ida_time",
      "a_star_nodes", "rev_a_star_nodes", "bae_nodes", "nbs_nodes", "mm_nodes", "ida_nodes", "rev_ida_nodes",
      "bihs_bloom_time_50pct", "bihs_bloom_time_10pct", "bihs_bloom_time_1pct",
      "bihs_bloom_nodes_50pct", "bihs_bloom_nodes_10pct", "bihs_bloom_nodes_1pct"};
  for(size_t i = 0; i < headers.size(); ++i) {
    log << headers[i];
    if (i < headers.size() - 1) log << ",";
  }
  log << "\n";

  std::ofstream convLog("bloom_convergence.csv");
  convLog << "instance,size_kib,ratio,total_depth,iteration,n_inserted,n_unique,estimated_fp,bits_set,fill_ratio,expected_fill_ratio,materialized_forward,materialized_backward,materialized_total\n";

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
                  << " | NBS: " << r.nbsTime << "s/" << r.nbsNodeExpanded << "n"
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
            << r.nbsTime << ","
            << r.mmTime << ","
            << r.idaTime << ","
            << r.revIdaTime << ","
            << r.aStarNodeExpanded << "," << r.revAStarNodeExpanded << ","
            << r.baeNodeExpanded << "," << r.nbsNodeExpanded << "," << r.mmNodeExpanded << ","
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

    try {
      t.StartTimer();
      ida.GetPath(&mnp, puzzle, goal, pathIDA);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      printf("IDA* ran out of memory\n");
    }
    double idaTime = t.GetElapsedTime();

    std::cout << "IDAStar Solve time: " << idaTime << std::endl;

    try {
      t.StartTimer();
      pathBiHS = bihs.GetPath(puzzle, goal);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      printf("BiHS-Bloom ran out of memory\n");
    }
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
