#include "Driver.h"
#include "IncrementalIDA.h"
#include "MNPuzzle.h"
#include "TemplateAStar.h"
#include "STPInstances.h"
#include "Timer.h"
#include "BiHSBloom.h"
#include <ext/hash_map>
#include "IDTHSwTrans.h"
#include "BAE.h"
#include "MM.h"
#include "IDAStar.h"
#include "ParallelIDAStar.h"
#include "NBS.h"

#include "PancakeInstances.h"
#include "RC.h"
#include "RubiksCube.h"
#include "RubiksInstances.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
#include <limits>

static constexpr double SKIPPED_TIME = -3.0;
static constexpr int NUM_BIHS_RUNS = 12;
static constexpr bool WRITE_BIHS_PARAM_LOG = true;
static constexpr bool WRITE_CONVERGENCE_LOG = true;
static constexpr unsigned long IDTHS_DEFAULT_STATES_BOUND = 1000000;
static constexpr unsigned long IDTHS_MIN_STATES_BOUND = 2;
static constexpr int IDTHS_SECONDS_LIMIT = 1800;
static constexpr int RUBIK_FIXED_SECONDS_LIMIT = 23 * 60 * 60;
static constexpr int RUBIK_FIXED_MEMORY_GIB = 128;
static constexpr int RUBIK_FIXED_BLOOM_SIZE_KIB = RUBIK_FIXED_MEMORY_GIB * 1024 * 1024;
static constexpr int RUBIK_FIXED_K_HASHES = 1;
static constexpr int RUBIK_TOTAL_INSTANCES = 10;
static constexpr const char *RUBIK_INSTANCE_SET = "korf";
static bool gRunFullBaselines = false;
static std::string gRubikPDBDir = "results/pdb/rubik";
static bool gBuildRubikPDBs = false;

static const char *SPLIT_RESULTS_HEADER =
    "domain,instance,algorithm,ratio,status,time,nodes,necessary_nodes,storage_states,"
    "size_kib,k_hashes,fp_est,solution_length,k_mode,split_mode,scan_schema,bound_cycles,"
    "forward_scans,backward_scans,table_full_backward_scans,end_cycle_backward_scans,"
    "type_split_scans,materialization_scans,intersection_scans,frontier_scans,total_scans\n";

static const char *SPLIT_CALIBRATION_HEADER =
    "domain,instance,algorithm,status,time,nodes,solution_length,memory_items,frontier_items\n";

static const char *CONVERGENCE_HEADER =
    "instance,size_kib,ratio,total_depth,iteration,n_inserted,n_unique,estimated_fp,bits_set,"
    "fill_ratio,expected_fill_ratio,materialized_forward,materialized_backward,materialized_total,"
    "phase,type_index,type_count,bound_cycle,forward_depth,backward_depth,"
    "cumulative_forward_scans,cumulative_backward_scans,cumulative_type_scans,"
    "first_forward_work,first_backward_work,observed_next_f,k_mode,k_hashes,split_mode\n";

struct BiHSRunConfig {
  double ratio;
  const char *ratioLabel;
  const char *ratioSlug;
  const char *kMode;
  const char *splitMode;
};

static constexpr BiHSRunConfig BIHS_RUNS[NUM_BIHS_RUNS] = {
    {0.5,  "50%", "50pct", "k1",    "idths_workload"},
    {0.5,  "50%", "50pct", "optk",  "idths_workload"},
    {0.5,  "50%", "50pct", "rootk", "idths_workload"},
    {0.1,  "10%", "10pct", "k1",    "idths_workload"},
    {0.1,  "10%", "10pct", "optk",  "idths_workload"},
    {0.1,  "10%", "10pct", "rootk", "idths_workload"},
    {0.01, "1%",  "1pct",  "k1",    "idths_workload"},
    {0.01, "1%",  "1pct",  "optk",  "idths_workload"},
    {0.01, "1%",  "1pct",  "rootk", "idths_workload"},
    {0.001, "0.1%", "0_1pct", "k1",    "idths_workload"},
    {0.001, "0.1%", "0_1pct", "optk",  "idths_workload"},
    {0.001, "0.1%", "0_1pct", "rootk", "idths_workload"},
};

static double fp_rate(int k, double n, double m) {
  return std::pow(1.0 - std::exp(-static_cast<double>(k) * n / m), k);
}

static int choose_k(double n, double m, double target = 0.01) {
  n = std::max(1.0, n);
  m = std::max(1.0, m);
  int k_opt = std::max(1, static_cast<int>(std::round((m / n) * std::log(2.0))));

  if (fp_rate(k_opt, n, m) > target) {
    return k_opt;
  }

  for (int k = 1; k <= k_opt; ++k) {
    if (fp_rate(k, n, m) <= target) {
      return k;
    }
  }

  return k_opt;
}

static int ComputeKHashes(const BiHSRunConfig &run, double estimatedItems, double bloomBits)
{
  int optimizedKHashes = std::max(1, static_cast<int>(
      std::round((bloomBits / std::max(1.0, estimatedItems)) * std::log(2.0))));
  if (std::strcmp(run.kMode, "k1") == 0)
    return 1;
  if (std::strcmp(run.kMode, "rootk") == 0)
    return std::max(1, static_cast<int>(std::round(std::sqrt(optimizedKHashes))));
  return optimizedKHashes;
}

class BenchmarkRC : public RubiksCube {
public:
  BenchmarkRC()
  {
    if (gRubikPDBDir.empty())
      throw std::runtime_error("Rubik runs require --rubik-pdb-dir <directory>");

    RubiksState goal;
    goal.Reset();
    const std::vector<int> noPieces;
    const std::vector<int> edges1 = {1, 3, 8, 9, 10, 11};
    const std::vector<int> edges2 = {0, 2, 4, 5, 6, 7};
    const std::vector<int> corners = {0, 1, 2, 3, 4, 5, 6, 7};

    pdb1 = std::make_shared<RubikPDB>(this, goal, edges1, noPieces);
    pdb2 = std::make_shared<RubikPDB>(this, goal, edges2, noPieces);
    pdb3 = std::make_shared<RubikPDB>(this, goal, noPieces, corners);

    LoadOrBuild(*pdb1, goal);
    LoadOrBuild(*pdb2, goal);
    LoadOrBuild(*pdb3, goal);

    arbitrary1 = std::make_shared<RubikArbitraryGoalPDB>(pdb1.get());
    arbitrary2 = std::make_shared<RubikArbitraryGoalPDB>(pdb2.get());
    arbitrary3 = std::make_shared<RubikArbitraryGoalPDB>(pdb3.get());
    heuristic.lookups.push_back({kMaxNode, 1, 3});
    heuristic.lookups.push_back({kLeafNode, 0, 0});
    heuristic.lookups.push_back({kLeafNode, 1, 0});
    heuristic.lookups.push_back({kLeafNode, 2, 0});
    heuristic.heuristics.push_back(arbitrary1.get());
    heuristic.heuristics.push_back(arbitrary2.get());
    heuristic.heuristics.push_back(arbitrary3.get());
  }

  double HCost(const RubiksState &from, const RubiksState &to) const override
  { return heuristic.HCost(from, to); }

  double HCost(const RubiksState &from, const RubiksState &to, double) const override
  { return heuristic.HCost(from, to); }

  uint64_t GetStateHash(const RubiksState &node) const override
  { return BiHSBloomFilter<RubiksState>::stable_fingerprint(node); }

private:
  void LoadOrBuild(RubikPDB &pdb, const RubiksState &goal)
  {
    if (pdb.Load(gRubikPDBDir.c_str()))
      return;
    if (!gBuildRubikPDBs) {
      throw std::runtime_error(
          "Missing Rubik PDB in '" + gRubikPDBDir +
          "'. Prepare it once with --rubik --prepare-rubik-pdbs --rubik-pdb-dir <directory>");
    }
    unsigned int threads = std::min(64u, std::max(1u, std::thread::hardware_concurrency()));
    const char *slurmCPUs = std::getenv("SLURM_CPUS_PER_TASK");
    if (slurmCPUs != nullptr) {
      char *end = nullptr;
      const unsigned long allocated = std::strtoul(slurmCPUs, &end, 10);
      if (end != slurmCPUs && *end == '\0' && allocated > 0)
        threads = std::min(threads, static_cast<unsigned int>(allocated));
    }
    pdb.BuildPDB(goal, threads);
    pdb.Save(gRubikPDBDir.c_str());
  }

  std::shared_ptr<RubikPDB> pdb1;
  std::shared_ptr<RubikPDB> pdb2;
  std::shared_ptr<RubikPDB> pdb3;
  std::shared_ptr<RubikArbitraryGoalPDB> arbitrary1;
  std::shared_ptr<RubikArbitraryGoalPDB> arbitrary2;
  std::shared_ptr<RubikArbitraryGoalPDB> arbitrary3;
  Heuristic<RubiksState> heuristic;
};

struct STPResult {
  int instance;
  int solutionLength;
  double aStarTime;
  double revAStarTime;
  double baeTime;
  double mmTime;
  double nbsTime;
  double idaTime;
  double parallelIdaTime;
  double revIdaTime;
  std::array<double, NUM_BIHS_RUNS> bihsTime;
  std::array<double, NUM_BIHS_RUNS> idthsTransTime;

  size_t aStarNodeExpanded;
  size_t revAStarNodeExpanded;
  size_t baeNodeExpanded;
  size_t mmNodeExpanded;
  size_t nbsNodeExpanded;
  size_t idaNodeExpanded;
  size_t parallelIdaNodeExpanded;
  size_t revIdaNodeExpanded;
  std::array<size_t, NUM_BIHS_RUNS> bihsNodeExpanded;
  std::array<size_t, NUM_BIHS_RUNS> idthsTransNodeExpanded;
  std::array<size_t, NUM_BIHS_RUNS> idthsTransNecessaryExpanded;
  std::array<unsigned long, NUM_BIHS_RUNS> idthsTransStorage;
  std::array<uint64_t, NUM_BIHS_RUNS> bihsBoundCycles;
  std::array<uint64_t, NUM_BIHS_RUNS> bihsForwardScans;
  std::array<uint64_t, NUM_BIHS_RUNS> bihsBackwardScans;
  std::array<uint64_t, NUM_BIHS_RUNS> bihsTypeSplitScans;
  std::array<uint64_t, NUM_BIHS_RUNS> bihsExtractionScans;
  std::array<uint64_t, NUM_BIHS_RUNS> idthsBoundCycles;
  std::array<uint64_t, NUM_BIHS_RUNS> idthsForwardScans;
  std::array<uint64_t, NUM_BIHS_RUNS> idthsBackwardScans;
  std::array<uint64_t, NUM_BIHS_RUNS> idthsTableFullBackwardScans;
  std::array<uint64_t, NUM_BIHS_RUNS> idthsEndCycleBackwardScans;
};

static void MarkFullBaselineSkipped(STPResult &result, int instance)
{
  result.baeTime = SKIPPED_TIME;
  result.nbsTime = SKIPPED_TIME;
  result.idaTime = SKIPPED_TIME;
  result.revIdaTime = SKIPPED_TIME;
  result.parallelIdaTime = SKIPPED_TIME;
  std::cout << "[" << instance
            << "] Skipping BAE*, NBS, IDA*, Rev-IDA*, and Parallel IDA*; use --full to run them\n"
            << std::flush;
}

static std::vector<std::string> SplitCSVLine(const std::string &line)
{
  std::vector<std::string> fields;
  std::stringstream ss(line);
  std::string field;
  while (std::getline(ss, field, ','))
    fields.push_back(field);
  return fields;
}

static const BiHSRunConfig *FindBiHSRun(double ratio, const std::string &kMode)
{
  for (const auto &run : BIHS_RUNS) {
    if (std::fabs(run.ratio - ratio) < 1e-12 && run.kMode == kMode)
      return &run;
  }
  return nullptr;
}

struct STPSplitParams {
  bool found = false;
  bool usable = false;
  int solutionLength = -1;
  size_t minMemoryItems = 0;
  size_t frontierItems = 0;
  double maxBaselineTime = -1;
  std::string reason;
};

struct ScanMetrics {
  uint64_t boundCycles = 0;
  uint64_t forwardScans = 0;
  uint64_t backwardScans = 0;
  uint64_t tableFullBackwardScans = 0;
  uint64_t endCycleBackwardScans = 0;
  uint64_t typeSplitScans = 0;
  uint64_t materializationScans = 0;
  uint64_t intersectionScans = 0;

  uint64_t FrontierScans() const { return forwardScans + backwardScans; }
  uint64_t TotalScans() const { return FrontierScans() + materializationScans + intersectionScans; }
};

static STPSplitParams LoadSplitParams(const std::string &paramsFile, const std::string &domain, int instance)
{
  STPSplitParams params;
  std::ifstream input(paramsFile);
  if (!input)
    throw std::runtime_error("Unable to open params input: " + paramsFile);

  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    if (line.empty())
      continue;
    auto fields = SplitCSVLine(line);
    if (fields.size() < 8)
      continue;
    if (fields[0] != domain || std::stoi(fields[1]) != instance)
      continue;

    params.found = true;
    params.usable = fields[2] == "ok";
    params.solutionLength = std::stoi(fields[3]);
    params.minMemoryItems = static_cast<size_t>(std::stoull(fields[4]));
    params.frontierItems = static_cast<size_t>(std::stoull(fields[5]));
    params.maxBaselineTime = std::stod(fields[6]);
    params.reason = fields[7];
    return params;
  }

  return params;
}

static void WriteSplitResultRow(std::ofstream &out, const std::string &domain, int instance, const std::string &algorithm,
                                double ratio, const std::string &status, double time,
                                size_t nodes, size_t necessaryNodes, unsigned long storageStates,
                                int sizeKiB, int kHashes, double fpEst, int solutionLength,
                                const std::string &kMode, const std::string &splitMode,
                                const ScanMetrics &scans = ScanMetrics{})
{
  out << domain << "," << instance << "," << algorithm << "," << ratio << "," << status << ","
      << time << "," << nodes << "," << necessaryNodes << "," << storageStates << ","
      << sizeKiB << "," << kHashes << "," << fpEst << "," << solutionLength << ","
      << kMode << "," << splitMode << ",scan_v1,"
      << scans.boundCycles << "," << scans.forwardScans << "," << scans.backwardScans << ","
      << scans.tableFullBackwardScans << "," << scans.endCycleBackwardScans << ","
      << scans.typeSplitScans << "," << scans.materializationScans << ","
      << scans.intersectionScans << "," << scans.FrontierScans() << "," << scans.TotalScans() << "\n";
}

static unsigned long FixedRubikStorageStates(const RubiksState &sample)
{
  long double memoryBits = static_cast<long double>(RUBIK_FIXED_BLOOM_SIZE_KIB) * 8192.0L;
  long double stateBits = static_cast<long double>(std::max<size_t>(1, get_state_size(sample)));
  long double states = std::floor(memoryBits / stateBits);
  long double maxStates = static_cast<long double>(std::numeric_limits<unsigned long>::max());
  if (states > maxStates)
    return std::numeric_limits<unsigned long>::max();
  return std::max<unsigned long>(static_cast<unsigned long>(states), IDTHS_MIN_STATES_BOUND);
}

static void runSTPCalibrationJob(int instance, const std::string &algorithm,
                                 const std::string &outputFile)
{
  std::ofstream out(outputFile);
  if (!out)
    throw std::runtime_error("Unable to open calibration output: " + outputFile);
  out << SPLIT_CALIBRATION_HEADER;

  MNPuzzle<MN_SIZE, MN_SIZE> mnp;
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  goal.Reset();
  MNPuzzleState<MN_SIZE, MN_SIZE> puzzle = STP::GetKorfInstance(instance);
  Timer t;

  std::string status = "ok";
  double elapsed = -1;
  size_t nodes = 0;
  int solutionLength = -1;
  size_t memoryItems = 0;
  size_t frontierItems = 0;

  try {
    if (algorithm == "astar") {
      TemplateAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> astar;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      std::cout << "[" << instance << "] Calibrating A*..." << std::flush;
      t.StartTimer();
      astar.GetPath(&mnp, puzzle, goal, path);
      t.EndTimer();
      elapsed = t.GetElapsedTime();
      nodes = astar.GetNodesExpanded();
      solutionLength = static_cast<int>(path.size()) - 1;
      memoryItems = astar.GetNumItems();
    } else if (algorithm == "rev_astar") {
      TemplateAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> astar;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      std::cout << "[" << instance << "] Calibrating Rev-A*..." << std::flush;
      t.StartTimer();
      astar.GetPath(&mnp, goal, puzzle, path);
      t.EndTimer();
      elapsed = t.GetElapsedTime();
      nodes = astar.GetNodesExpanded();
      solutionLength = static_cast<int>(path.size()) - 1;
      memoryItems = astar.GetNumItems();
    } else if (algorithm == "mm") {
      MM<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>> mm;
      std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;
      std::cout << "[" << instance << "] Calibrating MM..." << std::flush;
      t.StartTimer();
      mm.GetPath(&mnp, puzzle, goal, &mnp, &mnp, path);
      t.EndTimer();
      elapsed = t.GetElapsedTime();
      nodes = mm.GetNodesExpanded();
      solutionLength = static_cast<int>(path.size()) - 1;
      memoryItems = mm.GetNumForwardItems() + mm.GetNumBackwardItems();
      frontierItems = mm.GetNumForwardItems();
    } else {
      throw std::runtime_error("Unsupported calibration algorithm: " + algorithm);
    }
  }
  catch (const std::bad_alloc&) {
    t.EndTimer();
    status = "oom";
    elapsed = -2.0;
  }

  out << "stp," << instance << "," << algorithm << "," << status << ","
      << elapsed << "," << nodes << "," << solutionLength << ","
      << memoryItems << "," << frontierItems << "\n";
  std::cout << " " << status << " (" << elapsed << "s, " << nodes << "n)\n" << std::flush;
}

static void runSTPSplitAlgorithmJob(int instance, const std::string &algorithm, double ratio,
                                    const std::string &kMode,
                                    const std::string &paramsFile, const std::string &outputFile,
                                    const std::string &convergenceFile)
{
  std::ofstream out(outputFile);
  if (!out)
    throw std::runtime_error("Unable to open split result output: " + outputFile);
  out << SPLIT_RESULTS_HEADER;

  STPSplitParams params = LoadSplitParams(paramsFile, "stp", instance);
  if (!params.found || !params.usable) {
    WriteSplitResultRow(out, "stp", instance, algorithm, ratio, "missing_params", -3.0, 0, 0, 0, 0, 0, 0.0, -1, kMode, "");
    std::cout << "[" << instance << "] Missing calibration params; skipping " << algorithm << "\n";
    return;
  }

  std::string effectiveKMode = kMode.empty() ? "optk" : kMode;
  const BiHSRunConfig *run = FindBiHSRun(ratio, effectiveKMode);
  if (run == nullptr)
    throw std::runtime_error("Unsupported ratio/k-mode for split run: " + std::to_string(ratio) + "/" + effectiveKMode);

  MNPuzzle<MN_SIZE, MN_SIZE> mnp;
  MNPuzzleState<MN_SIZE, MN_SIZE> goal;
  goal.Reset();
  MNPuzzleState<MN_SIZE, MN_SIZE> puzzle = STP::GetKorfInstance(instance);
  Timer t;

  double bihsTimeLimit = std::max(params.maxBaselineTime * 20.0, 120.0);
  size_t frontierItems = params.frontierItems;
  if (frontierItems == 0)
    throw std::runtime_error("Missing MM frontier for instance " + std::to_string(instance));

  if (algorithm == "bihs_bloom") {
    int sizeKiB = std::max(1, static_cast<int>(
        std::round((static_cast<double>(params.minMemoryItems) * get_state_size(puzzle) / 8192.0) * ratio)));
    double bloomBits = sizeKiB * 8192.0;
    double estimatedFrontierItems = std::max(1.0, static_cast<double>(frontierItems));
    int kHashes = ComputeKHashes(*run, estimatedFrontierItems, bloomBits);
    double fpEst = fp_rate(kHashes, estimatedFrontierItems, bloomBits);

    using STPBiHSBloom = BiHSBloom<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>>;
    STPBiHSBloom bihs(sizeKiB, kHashes, bihsTimeLimit);
    bool outOfMemory = false;
    std::vector<slideDir> path;
    std::cout << "[" << instance << "] Split BiHS-Bloom(" << run->ratioLabel << ", "
              << run->kMode << ", " << run->splitMode << ")..." << std::flush;
    try {
      t.StartTimer();
      path = bihs.GetPath(puzzle, goal);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      outOfMemory = true;
    }

    std::string status = outOfMemory ? "oom" : (bihs.hasTimedOut() || path.empty() ? "timeout" : "ok");
    double elapsed = outOfMemory ? -2.0 : (status == "ok" ? t.GetElapsedTime() : -1.0);
    int solutionLength = status == "ok" ? static_cast<int>(path.size()) : params.solutionLength;
    if (status == "ok" && params.solutionLength >= 0 && solutionLength != params.solutionLength) {
      throw std::runtime_error("Split BiHS-Bloom returned a non-optimal STP path on instance " +
                               std::to_string(instance));
    }
    const auto &bihsScans = bihs.GetScanStats();
    ScanMetrics scanMetrics{bihsScans.boundCycles, bihsScans.forwardBloomScans,
                            bihsScans.backwardBloomScans, 0, 0, bihsScans.typeSplitScans,
                            bihsScans.materializationScans, bihsScans.intersectionScans};
    WriteSplitResultRow(out, "stp", instance, algorithm, ratio, status, elapsed,
                        bihs.GetTotalNodesExpanded(), 0, 0, sizeKiB, kHashes, fpEst, solutionLength,
                        run->kMode, run->splitMode, scanMetrics);

    if (WRITE_CONVERGENCE_LOG) {
      std::ofstream conv(convergenceFile);
      if (!conv)
        throw std::runtime_error("Unable to open convergence output: " + convergenceFile);
      conv << CONVERGENCE_HEADER;
      for (const auto &s : bihs.GetIterStats())
        conv << instance << "," << sizeKiB << "," << ratio << ","
             << s.totalDepth << "," << s.iteration << ","
             << s.nInserted << "," << s.nUnique << "," << s.estimatedFP << ","
             << s.bitsSet << "," << s.fillRatio << "," << s.expectedFillRatio << ","
             << s.materializedForwardStates << "," << s.materializedBackwardStates << ","
             << s.materializedTotalStates << "," << (s.isTypeSplit ? "type" : "iter") << ","
             << s.typeIndex << "," << s.typeCount << ","
             << s.boundCycle << "," << s.forwardDepth << "," << s.backwardDepth << ","
             << s.cumulativeForwardBloomScans << "," << s.cumulativeBackwardBloomScans << ","
             << s.cumulativeTypeSplitScans << "," << s.firstForwardWork << ","
             << s.firstBackwardWork << "," << s.observedNextF << ","
             << run->kMode << "," << kHashes << "," << run->splitMode << "\n";
    }
    std::cout << " " << status << " (" << elapsed << "s, " << bihs.GetTotalNodesExpanded() << "n)\n" << std::flush;
  } else if (algorithm == "idths_trans") {
    unsigned long storage = std::max<unsigned long>(
        static_cast<unsigned long>(params.minMemoryItems * ratio), IDTHS_MIN_STATES_BOUND);
    IDTHSwTrans<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, false> idthsTrans(true, true, true, 1, false);
    bool solved = false;
    bool outOfMemory = false;
    std::cout << "[" << instance << "] Split IDTHSwTrans(" << run->ratioLabel << ")..." << std::flush;
    try {
      t.StartTimer();
      solved = idthsTrans.GetPath(&mnp, puzzle, goal, IDTHS_SECONDS_LIMIT, storage);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      outOfMemory = true;
    }

    std::string status = outOfMemory ? "oom" : (solved ? "ok" : "timeout");
    double elapsed = outOfMemory ? -2.0 : (solved ? t.GetElapsedTime() : -1.0);
    int solutionLength = solved ? static_cast<int>(idthsTrans.getPathLength()) : params.solutionLength;
    if (solved && params.solutionLength >= 0 && solutionLength != params.solutionLength) {
      throw std::runtime_error("Split IDTHSwTrans returned a non-optimal STP length on instance " +
                               std::to_string(instance));
    }
    const auto &idthsScans = idthsTrans.GetScanStats();
    ScanMetrics scanMetrics{idthsScans.boundCycles, idthsScans.forwardScans,
                            idthsScans.backwardScans, idthsScans.backwardTableFullScans,
                            idthsScans.backwardEndOfCycleScans, 0, 0, 0};
    WriteSplitResultRow(out, "stp", instance, algorithm, ratio, status, elapsed,
                        idthsTrans.GetNodesExpanded(), idthsTrans.GetNecessaryExpansions(),
                        storage, 0, 0, 0.0, solutionLength, "", "", scanMetrics);
    std::cout << " " << status << " (" << elapsed << "s, " << idthsTrans.GetNodesExpanded() << "n)\n" << std::flush;
  } else {
    throw std::runtime_error("Unsupported split run algorithm: " + algorithm);
  }
}

static void runRubikCalibrationJob(int instance, const std::string &algorithm,
                                   const std::string &outputFile)
{
  std::ofstream out(outputFile);
  if (!out)
    throw std::runtime_error("Unable to open calibration output: " + outputFile);
  out << SPLIT_CALIBRATION_HEADER;

  BenchmarkRC rubik;
  RubiksState goal;
  RubiksState puzzle;
  goal.Reset();
  RubiksCubeInstances::GetKorfRubikInstance(puzzle, instance);
  Timer t;

  std::string status = "ok";
  double elapsed = -1;
  size_t nodes = 0;
  int solutionLength = -1;
  size_t memoryItems = 0;
  size_t frontierItems = 0;

  try {
    if (algorithm == "astar") {
      TemplateAStar<RubiksState, RubiksAction, BenchmarkRC> astar;
      std::vector<RubiksState> path;
      std::cout << "[" << instance << "] Calibrating Rubik A*..." << std::flush;
      t.StartTimer();
      astar.GetPath(&rubik, puzzle, goal, path);
      t.EndTimer();
      elapsed = t.GetElapsedTime();
      nodes = astar.GetNodesExpanded();
      solutionLength = static_cast<int>(path.size()) - 1;
      memoryItems = astar.GetNumItems();
    } else if (algorithm == "rev_astar") {
      TemplateAStar<RubiksState, RubiksAction, BenchmarkRC> astar;
      std::vector<RubiksState> path;
      std::cout << "[" << instance << "] Calibrating Rubik Rev-A*..." << std::flush;
      t.StartTimer();
      astar.GetPath(&rubik, goal, puzzle, path);
      t.EndTimer();
      elapsed = t.GetElapsedTime();
      nodes = astar.GetNodesExpanded();
      solutionLength = static_cast<int>(path.size()) - 1;
      memoryItems = astar.GetNumItems();
    } else if (algorithm == "mm") {
      MM<RubiksState, RubiksAction, BenchmarkRC> mm;
      std::vector<RubiksState> path;
      std::cout << "[" << instance << "] Calibrating Rubik MM..." << std::flush;
      t.StartTimer();
      mm.GetPath(&rubik, puzzle, goal, &rubik, &rubik, path);
      t.EndTimer();
      elapsed = t.GetElapsedTime();
      nodes = mm.GetNodesExpanded();
      solutionLength = static_cast<int>(path.size()) - 1;
      memoryItems = mm.GetNumForwardItems() + mm.GetNumBackwardItems();
      frontierItems = mm.GetNumForwardItems();
    } else {
      throw std::runtime_error("Unsupported calibration algorithm: " + algorithm);
    }
  }
  catch (const std::bad_alloc&) {
    t.EndTimer();
    status = "oom";
    elapsed = -2.0;
  }

  out << "rubik," << instance << "," << algorithm << "," << status << ","
      << elapsed << "," << nodes << "," << solutionLength << ","
      << memoryItems << "," << frontierItems << "\n";
  std::cout << " " << status << " (" << elapsed << "s, " << nodes << "n)\n" << std::flush;
}

static void runRubikSplitAlgorithmJob(int instance, const std::string &algorithm, double ratio,
                                      const std::string &kMode,
                                      const std::string &paramsFile, const std::string &outputFile,
                                      const std::string &convergenceFile)
{
  std::ofstream out(outputFile);
  if (!out)
    throw std::runtime_error("Unable to open split result output: " + outputFile);
  out << SPLIT_RESULTS_HEADER;

  STPSplitParams params = LoadSplitParams(paramsFile, "rubik", instance);
  if (!params.found || !params.usable) {
    WriteSplitResultRow(out, "rubik", instance, algorithm, ratio, "missing_params", -3.0, 0, 0, 0, 0, 0, 0.0, -1, kMode, "");
    std::cout << "[" << instance << "] Missing calibration params; skipping Rubik " << algorithm << "\n";
    return;
  }

  std::string effectiveKMode = kMode.empty() ? "optk" : kMode;
  const BiHSRunConfig *run = FindBiHSRun(ratio, effectiveKMode);
  if (run == nullptr)
    throw std::runtime_error("Unsupported ratio/k-mode for split run: " + std::to_string(ratio) + "/" + effectiveKMode);

  BenchmarkRC rubik;
  RubiksState goal;
  RubiksState puzzle;
  goal.Reset();
  RubiksCubeInstances::GetKorfRubikInstance(puzzle, instance);
  Timer t;

  double bihsTimeLimit = std::max(params.maxBaselineTime * 20.0, 120.0);
  if (params.frontierItems == 0)
    throw std::runtime_error("Missing Rubik MM frontier for instance " + std::to_string(instance));

  if (algorithm == "bihs_bloom") {
    int sizeKiB = std::max(1, static_cast<int>(
        std::round((static_cast<double>(params.minMemoryItems) * get_state_size(puzzle) / 8192.0) * ratio)));
    double bloomBits = sizeKiB * 8192.0;
    double estimatedFrontierItems = std::max(1.0, static_cast<double>(params.frontierItems));
    int kHashes = ComputeKHashes(*run, estimatedFrontierItems, bloomBits);
    double fpEst = fp_rate(kHashes, estimatedFrontierItems, bloomBits);

    using RubikBiHSBloom = BiHSBloom<RubiksState, RubiksAction, BenchmarkRC>;
    RubikBiHSBloom bihs(sizeKiB, kHashes, bihsTimeLimit);
    bool outOfMemory = false;
    std::vector<RubiksAction> path;
    std::cout << "[" << instance << "] Split Rubik BiHS-Bloom(" << run->ratioLabel << ", "
              << run->kMode << ", " << run->splitMode << ")..." << std::flush;
    try {
      t.StartTimer();
      path = bihs.GetPath(puzzle, goal);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      outOfMemory = true;
    }

    std::string status = outOfMemory ? "oom" : (bihs.hasTimedOut() || path.empty() ? "timeout" : "ok");
    double elapsed = outOfMemory ? -2.0 : (status == "ok" ? t.GetElapsedTime() : -1.0);
    int solutionLength = status == "ok" ? static_cast<int>(path.size()) : params.solutionLength;
    const auto &bihsScans = bihs.GetScanStats();
    ScanMetrics scanMetrics{bihsScans.boundCycles, bihsScans.forwardBloomScans,
                            bihsScans.backwardBloomScans, 0, 0, bihsScans.typeSplitScans,
                            bihsScans.materializationScans, bihsScans.intersectionScans};
    WriteSplitResultRow(out, "rubik", instance, algorithm, ratio, status, elapsed,
                        bihs.GetTotalNodesExpanded(), 0, 0, sizeKiB, kHashes, fpEst, solutionLength,
                        run->kMode, run->splitMode, scanMetrics);

    if (WRITE_CONVERGENCE_LOG) {
      std::ofstream conv(convergenceFile);
      if (!conv)
        throw std::runtime_error("Unable to open convergence output: " + convergenceFile);
      conv << CONVERGENCE_HEADER;
      for (const auto &s : bihs.GetIterStats())
        conv << instance << "," << sizeKiB << "," << ratio << ","
             << s.totalDepth << "," << s.iteration << ","
             << s.nInserted << "," << s.nUnique << "," << s.estimatedFP << ","
             << s.bitsSet << "," << s.fillRatio << "," << s.expectedFillRatio << ","
             << s.materializedForwardStates << "," << s.materializedBackwardStates << ","
             << s.materializedTotalStates << "," << (s.isTypeSplit ? "type" : "iter") << ","
             << s.typeIndex << "," << s.typeCount << ","
             << s.boundCycle << "," << s.forwardDepth << "," << s.backwardDepth << ","
             << s.cumulativeForwardBloomScans << "," << s.cumulativeBackwardBloomScans << ","
             << s.cumulativeTypeSplitScans << "," << s.firstForwardWork << ","
             << s.firstBackwardWork << "," << s.observedNextF << ","
             << run->kMode << "," << kHashes << "," << run->splitMode << "\n";
    }
    std::cout << " " << status << " (" << elapsed << "s, " << bihs.GetTotalNodesExpanded() << "n)\n" << std::flush;
  } else if (algorithm == "idths_trans") {
    unsigned long storage = std::max<unsigned long>(
        static_cast<unsigned long>(params.minMemoryItems * ratio), IDTHS_MIN_STATES_BOUND);
    IDTHSwTrans<RubiksState, RubiksAction, false> idthsTrans(true, true, true, 1, true);
    bool solved = false;
    bool outOfMemory = false;
    std::cout << "[" << instance << "] Split Rubik IDTHSwTrans(" << run->ratioLabel << ")..." << std::flush;
    try {
      t.StartTimer();
      solved = idthsTrans.GetPath(&rubik, puzzle, goal, IDTHS_SECONDS_LIMIT, storage);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      outOfMemory = true;
    }

    std::string status = outOfMemory ? "oom" : (solved ? "ok" : "timeout");
    double elapsed = outOfMemory ? -2.0 : (solved ? t.GetElapsedTime() : -1.0);
    int solutionLength = solved ? static_cast<int>(idthsTrans.getPathLength()) : params.solutionLength;
    const auto &idthsScans = idthsTrans.GetScanStats();
    ScanMetrics scanMetrics{idthsScans.boundCycles, idthsScans.forwardScans,
                            idthsScans.backwardScans, idthsScans.backwardTableFullScans,
                            idthsScans.backwardEndOfCycleScans, 0, 0, 0};
    WriteSplitResultRow(out, "rubik", instance, algorithm, ratio, status, elapsed,
                        idthsTrans.GetNodesExpanded(), idthsTrans.GetNecessaryExpansions(),
                        storage, 0, 0, 0.0, solutionLength, "", "", scanMetrics);
    std::cout << " " << status << " (" << elapsed << "s, " << idthsTrans.GetNodesExpanded() << "n)\n" << std::flush;
  } else {
    throw std::runtime_error("Unsupported split run algorithm: " + algorithm);
  }
}

static void runRubikFixedAlgorithmJob(int instance, const std::string &algorithm,
                                      const std::string &outputFile,
                                      const std::string &convergenceFile)
{
  std::ofstream out(outputFile);
  if (!out)
    throw std::runtime_error("Unable to open fixed Rubik result output: " + outputFile);
  out << SPLIT_RESULTS_HEADER;

  BenchmarkRC rubik;
  RubiksState goal;
  RubiksState puzzle;
  goal.Reset();
  RubiksCubeInstances::GetKorfRubikInstance(puzzle, instance);
  Timer t;

  constexpr double fixedRatio = 1.0;
  if (algorithm == "bihs_bloom") {
    const int sizeKiB = RUBIK_FIXED_BLOOM_SIZE_KIB;
    const int kHashes = RUBIK_FIXED_K_HASHES;
    const double fpEst = 0.0;

    using RubikBiHSBloom = BiHSBloom<RubiksState, RubiksAction, BenchmarkRC>;
    RubikBiHSBloom bihs(sizeKiB, kHashes, RUBIK_FIXED_SECONDS_LIMIT);
    bool outOfMemory = false;
    std::vector<RubiksAction> path;
    std::cout << "[" << instance << "] Fixed Rubik BiHS-Bloom(128GiB,k=1)..." << std::flush;
    try {
      t.StartTimer();
      path = bihs.GetPath(puzzle, goal);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      outOfMemory = true;
    }

    std::string status = outOfMemory ? "oom" : (bihs.hasTimedOut() || path.empty() ? "timeout" : "ok");
    double elapsed = outOfMemory ? -2.0 : (status == "ok" ? t.GetElapsedTime() : -1.0);
    int solutionLength = status == "ok" ? static_cast<int>(path.size()) : -1;
    const auto &bihsScans = bihs.GetScanStats();
    ScanMetrics scanMetrics{bihsScans.boundCycles, bihsScans.forwardBloomScans,
                            bihsScans.backwardBloomScans, 0, 0, bihsScans.typeSplitScans,
                            bihsScans.materializationScans, bihsScans.intersectionScans};
    WriteSplitResultRow(out, "rubik", instance, algorithm, fixedRatio, status, elapsed,
                        bihs.GetTotalNodesExpanded(), 0, 0, sizeKiB, kHashes, fpEst, solutionLength,
                        "k1", "idths_workload", scanMetrics);

    if (WRITE_CONVERGENCE_LOG) {
      std::ofstream conv(convergenceFile);
      if (!conv)
        throw std::runtime_error("Unable to open fixed Rubik convergence output: " + convergenceFile);
      conv << CONVERGENCE_HEADER;
      for (const auto &s : bihs.GetIterStats())
        conv << instance << "," << sizeKiB << "," << fixedRatio << ","
             << s.totalDepth << "," << s.iteration << ","
             << s.nInserted << "," << s.nUnique << "," << s.estimatedFP << ","
             << s.bitsSet << "," << s.fillRatio << "," << s.expectedFillRatio << ","
             << s.materializedForwardStates << "," << s.materializedBackwardStates << ","
             << s.materializedTotalStates << "," << (s.isTypeSplit ? "type" : "iter") << ","
             << s.typeIndex << "," << s.typeCount << ","
             << s.boundCycle << "," << s.forwardDepth << "," << s.backwardDepth << ","
             << s.cumulativeForwardBloomScans << "," << s.cumulativeBackwardBloomScans << ","
             << s.cumulativeTypeSplitScans << "," << s.firstForwardWork << ","
             << s.firstBackwardWork << "," << s.observedNextF << ",fixed," << kHashes << ",idths_workload\n";
    }
    std::cout << " " << status << " (" << elapsed << "s, " << bihs.GetTotalNodesExpanded() << "n)\n" << std::flush;
  } else if (algorithm == "idths_trans") {
    unsigned long storage = FixedRubikStorageStates(puzzle);
    IDTHSwTrans<RubiksState, RubiksAction, false> idthsTrans(true, true, true, 1, true);
    bool solved = false;
    bool outOfMemory = false;
    std::cout << "[" << instance << "] Fixed Rubik IDTHSwTrans(128GiB states=" << storage << ")..." << std::flush;
    try {
      t.StartTimer();
      solved = idthsTrans.GetPath(&rubik, puzzle, goal, RUBIK_FIXED_SECONDS_LIMIT, storage);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      outOfMemory = true;
    }

    std::string status = outOfMemory ? "oom" : (solved ? "ok" : "timeout");
    double elapsed = outOfMemory ? -2.0 : (solved ? t.GetElapsedTime() : -1.0);
    int solutionLength = solved ? static_cast<int>(idthsTrans.getPathLength()) : -1;
    const auto &idthsScans = idthsTrans.GetScanStats();
    ScanMetrics scanMetrics{idthsScans.boundCycles, idthsScans.forwardScans,
                            idthsScans.backwardScans, idthsScans.backwardTableFullScans,
                            idthsScans.backwardEndOfCycleScans, 0, 0, 0};
    WriteSplitResultRow(out, "rubik", instance, algorithm, fixedRatio, status, elapsed,
                        idthsTrans.GetNodesExpanded(), idthsTrans.GetNecessaryExpansions(),
                        storage, 0, 0, 0.0, solutionLength, "", "", scanMetrics);
    std::cout << " " << status << " (" << elapsed << "s, " << idthsTrans.GetNodesExpanded() << "n)\n" << std::flush;
  } else if (algorithm == "ida") {
    IDAStar<RubiksState, RubiksAction, false> ida;
    bool outOfMemory = false;
    std::vector<RubiksState> path;
    std::cout << "[" << instance << "] Fixed Rubik IDA*..." << std::flush;
    try {
      t.StartTimer();
      ida.GetPath(&rubik, puzzle, goal, path);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      outOfMemory = true;
    }

    std::string status = outOfMemory ? "oom" : (path.empty() ? "timeout" : "ok");
    double elapsed = outOfMemory ? -2.0 : (status == "ok" ? t.GetElapsedTime() : -1.0);
    int solutionLength = status == "ok" ? static_cast<int>(path.size()) - 1 : -1;
    WriteSplitResultRow(out, "rubik", instance, algorithm, fixedRatio, status, elapsed,
                        ida.GetNodesExpanded(), 0, 0, 0, 0, 0.0, solutionLength, "", "");
    std::cout << " " << status << " (" << elapsed << "s, " << ida.GetNodesExpanded() << "n)\n" << std::flush;
  } else {
    throw std::runtime_error("Unsupported fixed Rubik algorithm: " + algorithm);
  }
}

static constexpr int NUM_WORKERS = 1;
static constexpr int PANCAKE_SIZE = 20;

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
  result.parallelIdaTime = -1;
  result.revIdaTime = -1;
  result.bihsTime.fill(-1);
  result.idthsTransTime.fill(-1);
  result.bihsNodeExpanded.fill(0);
  result.idthsTransNodeExpanded.fill(0);
  result.idthsTransNecessaryExpanded.fill(0);
  result.idthsTransStorage.fill(0);
  result.aStarNodeExpanded = 0;
  result.revAStarNodeExpanded = 0;
  result.baeNodeExpanded = 0;
  result.mmNodeExpanded = 0;
  result.nbsNodeExpanded = 0;
  result.idaNodeExpanded = 0;
  result.parallelIdaNodeExpanded = 0;
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

  if (!gRunFullBaselines) {
    MarkFullBaselineSkipped(result, i);
  } else {
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

    {
      std::cout << "[" << i << "] Running NBS..." << std::flush;
      // NBS
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
  }

  {
      std::cout << "[" << i << "] Running MM..." << std::flush;
      // MM
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

  if (gRunFullBaselines) {
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

    std::cout << "[" << i << "] Running Parallel IDA*..." << std::flush;
    // Parallel IDA*
    {
      ParallelIDAStar<MNPuzzle<MN_SIZE, MN_SIZE>, MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir> ida;
      std::vector<slideDir> path;
      try {
        t.StartTimer();
        ida.GetPath(&mnp, puzzle, goal, path);
        t.EndTimer();
      }
      catch (const std::bad_alloc&) {
        t.EndTimer();
        printf("Parallel IDA* ran out of memory\n");
      }
      result.parallelIdaTime = t.GetElapsedTime();
      result.parallelIdaNodeExpanded = ida.GetNodesExpanded();
      result.solutionLength = static_cast<int>(path.size());
      std::cout << " done (" << result.parallelIdaTime << "s, " << result.parallelIdaNodeExpanded << "n)\n" << std::flush;
    }
  }

  double maxBaselineTime = std::max({result.aStarTime, result.revAStarTime, result.baeTime, result.nbsTime,
                                     result.mmTime, result.idaTime, result.revIdaTime, result.parallelIdaTime});
  double bihsTimeLimit = std::max(maxBaselineTime * 20.0, 120.0); // Set a minimum time limit of 120 seconds for BiHS-Bloom
  if (frontierSize == 0) {
    frontierSize = std::max<size_t>(1, minSize);
    std::cout << "[" << i << "] MM frontier unavailable; using minSize fallback for BiHS k estimate: "
              << frontierSize << "\n" << std::flush;
  }
  unsigned long idthsStatesQuantityBound = minSize > 0
      ? static_cast<unsigned long>(minSize)
      : IDTHS_DEFAULT_STATES_BOUND;
  std::cout << "[" << i << "] BiHS-Bloom timeout limit: " << bihsTimeLimit << "s\n" << std::flush;
  std::cout << "[" << i << "] IDTHSwTrans state bound baseline: "
            << idthsStatesQuantityBound << "\n" << std::flush;

  using STPBiHSBloom = BiHSBloom<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, MNPuzzle<MN_SIZE, MN_SIZE>>;

  // BiHS-Bloom
  for(int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
  {
    const BiHSRunConfig &run = BIHS_RUNS[runIdx];
    double ratio = run.ratio;
    int size_in_KiB = std::max(1, static_cast<int>(
        std::round((static_cast<double>(minSize) * get_state_size(puzzle) / 8192.0) * ratio))); // Convert bits to KiB

    double bloomBits = size_in_KiB * 8192.0;
    double estimatedFrontierItems = std::max(1.0, static_cast<double>(frontierSize));
    int k_hashes = ComputeKHashes(run, estimatedFrontierItems, bloomBits);

    // FP rate: (1 - e^(-k*n/m))^k where n=estimated items, m=Bloom bits.
    double fp_est = fp_rate(k_hashes, estimatedFrontierItems, bloomBits);

    std::cout << "[" << i << "] Running BiHS-Bloom(" << run.ratioLabel << ", " << run.kMode
              << ", " << run.splitMode
              << ", size=" << size_in_KiB << "KiB, k=" << k_hashes << ", fp_est=" << fp_est
              << ", limit=" << bihsTimeLimit << "s)..." << std::flush;

    STPBiHSBloom bihs(size_in_KiB, k_hashes, bihsTimeLimit);
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

    result.bihsTime[runIdx] = bihsOutOfMemory ? -2.0 : (bihs.hasTimedOut() ? -1.0 : t.GetElapsedTime());
    result.bihsNodeExpanded[runIdx] = bihs.GetTotalNodesExpanded();
    const auto &bihsScans = bihs.GetScanStats();
    result.bihsBoundCycles[runIdx] = bihsScans.boundCycles;
    result.bihsForwardScans[runIdx] = bihsScans.forwardBloomScans;
    result.bihsBackwardScans[runIdx] = bihsScans.backwardBloomScans;
    result.bihsTypeSplitScans[runIdx] = bihsScans.typeSplitScans;
    result.bihsExtractionScans[runIdx] = bihsScans.ExtractionScans();
    bool converged = !bihsOutOfMemory && !bihs.hasTimedOut() && !pathBiHS.empty();

    if (bihsOutOfMemory)
      std::cout << " OUT OF MEMORY (" << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;
    else if (!converged)
      std::cout << " TIMED OUT (" << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;
    else
      std::cout << " done (" << result.bihsTime[runIdx] << "s, " << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;

    if (WRITE_BIHS_PARAM_LOG) {
      std::lock_guard<std::mutex> lk(logMutex);
      log << "BIHS_PARAM," << i << "," << ratio << "," << size_in_KiB << "," << k_hashes << ","
          << result.bihsTime[runIdx] << "," << result.bihsNodeExpanded[runIdx] << ","
          << fp_est << "," << converged << "," << run.kMode << "," << run.splitMode << "\n";
    }
    if (WRITE_CONVERGENCE_LOG) {
      std::lock_guard<std::mutex> lk(convMutex);
      for (const auto &s : bihs.GetIterStats())
        convLog << i << "," << size_in_KiB << "," << ratio << ","
                << s.totalDepth << "," << s.iteration << ","
                << s.nInserted << "," << s.nUnique << "," << s.estimatedFP << ","
                << s.bitsSet << "," << s.fillRatio << "," << s.expectedFillRatio << ","
                << s.materializedForwardStates << "," << s.materializedBackwardStates << ","
                << s.materializedTotalStates << ","
                << (s.isTypeSplit ? "type" : "iter") << ","
                << s.typeIndex << "," << s.typeCount << ","
                << s.boundCycle << "," << s.forwardDepth << "," << s.backwardDepth << ","
                << s.cumulativeForwardBloomScans << "," << s.cumulativeBackwardBloomScans << ","
                << s.cumulativeTypeSplitScans << "," << s.firstForwardWork << ","
                << s.firstBackwardWork << "," << s.observedNextF << ","
                << run.kMode << "," << k_hashes << "," << run.splitMode << "\n";
      convLog.flush();
    }
    if (converged) {
      const int bihsLength = static_cast<int>(pathBiHS.size());
      if (result.solutionLength >= 0 && bihsLength != result.solutionLength) {
        throw std::runtime_error("BiHS-Bloom returned a non-optimal STP path on instance " +
                                 std::to_string(i) + ": got " + std::to_string(bihsLength) +
                                 ", expected " + std::to_string(result.solutionLength));
      }
      if (result.solutionLength < 0)
        result.solutionLength = bihsLength;
    }

    unsigned long idthsStorage = std::max<unsigned long>(
        static_cast<unsigned long>(idthsStatesQuantityBound * run.ratio),
        IDTHS_MIN_STATES_BOUND);
    result.idthsTransStorage[runIdx] = idthsStorage;

    std::cout << "[" << i << "] Running IDTHSwTrans(" << run.ratioLabel
              << ", states=" << idthsStorage << ", limit=" << IDTHS_SECONDS_LIMIT << "s)..."
              << std::flush;

    IDTHSwTrans<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir, false> idthsTrans(true, true, true, 1, false);
    bool idthsSolved = false;
    bool idthsOutOfMemory = false;
    try {
      t.StartTimer();
      idthsSolved = idthsTrans.GetPath(&mnp, puzzle, goal, IDTHS_SECONDS_LIMIT, idthsStorage);
      t.EndTimer();
    }
    catch (const std::bad_alloc&) {
      t.EndTimer();
      idthsOutOfMemory = true;
      printf("IDTHSwTrans ran out of memory\n");
    }

    result.idthsTransTime[runIdx] = idthsOutOfMemory ? -2.0 : (idthsSolved ? t.GetElapsedTime() : -1.0);
    result.idthsTransNodeExpanded[runIdx] = idthsTrans.GetNodesExpanded();
    result.idthsTransNecessaryExpanded[runIdx] = idthsTrans.GetNecessaryExpansions();
    const auto &idthsScans = idthsTrans.GetScanStats();
    result.idthsBoundCycles[runIdx] = idthsScans.boundCycles;
    result.idthsForwardScans[runIdx] = idthsScans.forwardScans;
    result.idthsBackwardScans[runIdx] = idthsScans.backwardScans;
    result.idthsTableFullBackwardScans[runIdx] = idthsScans.backwardTableFullScans;
    result.idthsEndCycleBackwardScans[runIdx] = idthsScans.backwardEndOfCycleScans;

    if (idthsOutOfMemory)
      std::cout << " OUT OF MEMORY (" << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
    else if (!idthsSolved)
      std::cout << " TIMED OUT (" << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
    else {
      const int idthsLength = static_cast<int>(idthsTrans.getPathLength());
      if (result.solutionLength >= 0 && idthsLength != result.solutionLength) {
        throw std::runtime_error("IDTHSwTrans returned a non-optimal STP length on instance " +
                                 std::to_string(i) + ": got " + std::to_string(idthsLength) +
                                 ", expected " + std::to_string(result.solutionLength));
      }
      if (result.solutionLength < 0)
        result.solutionLength = idthsLength;
      std::cout << " done (" << result.idthsTransTime[runIdx] << "s, "
                << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
    }
  }

  return result;
}

void solveSTP(int instanceStart, int instanceEnd,
              const std::string &benchmarkFile,
              const std::string &convergenceFile){
  std::ofstream log(benchmarkFile);
  if (!log) {
    throw std::runtime_error("Unable to open benchmark output: " + benchmarkFile);
  }
  std::vector<std::string> headers = {"instance", "solution_length",
      "a_star_time", "rev_a_star_time", "bae_time", "nbs_time", "mm_time", "ida_time", "parallel_ida_time", "rev_ida_time",
      "a_star_nodes", "rev_a_star_nodes", "bae_nodes", "nbs_nodes", "mm_nodes", "ida_nodes", "parallel_ida_nodes", "rev_ida_nodes",
      };
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("bihs_bloom_time_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("bihs_bloom_nodes_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("idths_trans_time_") + run.ratioSlug);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("idths_trans_nodes_") + run.ratioSlug);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("idths_trans_necessary_nodes_") + run.ratioSlug);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("idths_trans_storage_states_") + run.ratioSlug);
  }
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("bihs_bound_cycles_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("bihs_forward_scans_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("bihs_backward_scans_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("bihs_type_split_scans_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("bihs_extraction_scans_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_bound_cycles_") + run.ratioSlug + "_" + run.kMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_forward_scans_") + run.ratioSlug + "_" + run.kMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_backward_scans_") + run.ratioSlug + "_" + run.kMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_table_full_backward_scans_") + run.ratioSlug + "_" + run.kMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_end_cycle_backward_scans_") + run.ratioSlug + "_" + run.kMode);
  for(size_t i = 0; i < headers.size(); ++i) {
    log << headers[i];
    if (i < headers.size() - 1) log << ",";
  }
  log << "\n";

  std::ofstream convLog;
  if (WRITE_CONVERGENCE_LOG) {
    convLog.open(convergenceFile);
    if (!convLog) {
      throw std::runtime_error("Unable to open convergence output: " + convergenceFile);
    }
    convLog << CONVERGENCE_HEADER;
  }

  std::mutex logMutex;
  std::mutex convMutex;
  std::mutex coutMutex;
  std::atomic<int> nextInstance{instanceStart};

  auto worker = [&]() {
    while (true) {
      int i = nextInstance.fetch_add(1);
      if (i >= instanceEnd) break;

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
                  << " | Parallel IDA*: " << r.parallelIdaTime << "s/" << r.parallelIdaNodeExpanded << "n"
                  << " | Rev-IDA*: " << r.revIdaTime << "s/" << r.revIdaNodeExpanded << "n";
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
          const auto &run = BIHS_RUNS[runIdx];
          std::cout << " | BiHS-Bloom(" << run.ratioLabel << "," << run.kMode << "," << run.splitMode
                    << "): " << r.bihsTime[runIdx] << "s/" << r.bihsNodeExpanded[runIdx] << "n/"
                    << (r.bihsForwardScans[runIdx] + r.bihsBackwardScans[runIdx]) << "scans";
          std::cout << " | IDTHSwTrans(" << run.ratioLabel
                    << "): " << r.idthsTransTime[runIdx] << "s/"
                    << r.idthsTransNodeExpanded[runIdx] << "n/"
                    << (r.idthsForwardScans[runIdx] + r.idthsBackwardScans[runIdx]) << "scans";
        }
        std::cout << " | Length: " << r.solutionLength << std::endl;
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
            << r.parallelIdaTime << ","
            << r.revIdaTime << ","
            << r.aStarNodeExpanded << "," << r.revAStarNodeExpanded << ","
            << r.baeNodeExpanded << "," << r.nbsNodeExpanded << "," << r.mmNodeExpanded << ","
            << r.idaNodeExpanded << "," << r.parallelIdaNodeExpanded << "," << r.revIdaNodeExpanded << ","
            << r.bihsTime[0];
        for (int runIdx = 1; runIdx < NUM_BIHS_RUNS; ++runIdx) {
          log << "," << r.bihsTime[runIdx];
        }
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
          log << "," << r.bihsNodeExpanded[runIdx];
        }
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
          log << "," << r.idthsTransTime[runIdx];
        }
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
          log << "," << r.idthsTransNodeExpanded[runIdx];
        }
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
          log << "," << r.idthsTransNecessaryExpanded[runIdx];
        }
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
          log << "," << r.idthsTransStorage[runIdx];
        }
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.bihsBoundCycles[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.bihsForwardScans[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.bihsBackwardScans[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.bihsTypeSplitScans[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.bihsExtractionScans[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.idthsBoundCycles[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.idthsForwardScans[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.idthsBackwardScans[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.idthsTableFullBackwardScans[runIdx];
        for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
          log << "," << r.idthsEndCycleBackwardScans[runIdx];
        log << "\n";
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
  std::cout << "Results written to " << benchmarkFile << std::endl;
}

void solvePancake(int instanceStart, int instanceEnd,
                  const std::string &benchmarkFile,
                  const std::string &convergenceFile){
  std::ofstream log(benchmarkFile);
  if (!log) {
    throw std::runtime_error("Unable to open benchmark output: " + benchmarkFile);
  }
  std::vector<std::string> headers = {"instance", "solution_length",
      "a_star_time", "rev_a_star_time", "bae_time", "nbs_time", "mm_time", "ida_time", "parallel_ida_time", "rev_ida_time",
      "a_star_nodes", "rev_a_star_nodes", "bae_nodes", "nbs_nodes", "mm_nodes", "ida_nodes", "parallel_ida_nodes", "rev_ida_nodes",
      };
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("bihs_bloom_time_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("bihs_bloom_nodes_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("idths_trans_time_") + run.ratioSlug);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("idths_trans_nodes_") + run.ratioSlug);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("idths_trans_necessary_nodes_") + run.ratioSlug);
  }
  for (const auto &run : BIHS_RUNS) {
    headers.push_back(std::string("idths_trans_storage_states_") + run.ratioSlug);
  }
  for(size_t i = 0; i < headers.size(); ++i) {
    log << headers[i];
    if (i < headers.size() - 1) log << ",";
  }
  log << "\n";

  std::ofstream convLog;
  if (WRITE_CONVERGENCE_LOG) {
    convLog.open(convergenceFile);
    if (!convLog) {
      throw std::runtime_error("Unable to open convergence output: " + convergenceFile);
    }
    convLog << CONVERGENCE_HEADER;
  }

  std::mutex logMutex;
  std::mutex convMutex;

  for (int i = instanceStart; i < instanceEnd; i++) {
    STPResult result;
    result.instance = i;
    result.solutionLength = -1;
    result.aStarTime = -1;
    result.revAStarTime = -1;
    result.baeTime = -1;
    result.mmTime = -1;
    result.nbsTime = -1;
    result.idaTime = -1;
    result.parallelIdaTime = -1;
    result.revIdaTime = -1;
    result.bihsTime.fill(-1);
    result.idthsTransTime.fill(-1);
    result.bihsNodeExpanded.fill(0);
    result.idthsTransNodeExpanded.fill(0);
    result.idthsTransNecessaryExpanded.fill(0);
    result.idthsTransStorage.fill(0);
    result.aStarNodeExpanded = 0;
    result.revAStarNodeExpanded = 0;
    result.baeNodeExpanded = 0;
    result.mmNodeExpanded = 0;
    result.nbsNodeExpanded = 0;
    result.idaNodeExpanded = 0;
    result.parallelIdaNodeExpanded = 0;
    result.revIdaNodeExpanded = 0;

    size_t minSize = 0;
    size_t frontierSize = 0;

    PancakePuzzle<PANCAKE_SIZE> pancake;
    PancakePuzzleState<PANCAKE_SIZE> goal;
    goal.Reset();
    PancakePuzzleState<PANCAKE_SIZE> puzzle;
    if (!GetPancakeInstance(puzzle, i)) {
      std::cerr << "Unable to load Pancake challenge #" << i << std::endl;
      continue;
    }
    Timer t;

    std::cout << "[" << i << "] Running Pancake A*..." << std::flush;
    {
      TemplateAStar<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, PancakePuzzle<PANCAKE_SIZE>> astar;
      std::vector<PancakePuzzleState<PANCAKE_SIZE>> path;
      try {
        t.StartTimer();
        astar.GetPath(&pancake, puzzle, goal, path);
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

    std::cout << "[" << i << "] Running Pancake Rev-A*..." << std::flush;
    {
      TemplateAStar<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, PancakePuzzle<PANCAKE_SIZE>> astar;
      std::vector<PancakePuzzleState<PANCAKE_SIZE>> path;
      try {
        t.StartTimer();
        astar.GetPath(&pancake, goal, puzzle, path);
        t.EndTimer();
      }
      catch (const std::bad_alloc&) {
        t.EndTimer();
        printf("Rev-A* ran out of memory\n");
      }
      result.revAStarTime = t.GetElapsedTime();
      result.revAStarNodeExpanded = astar.GetNodesExpanded();
      result.solutionLength = static_cast<int>(path.size()) - 1;
      if (minSize == 0 || astar.GetNumItems() < minSize)
        minSize = astar.GetNumItems();
      std::cout << " done (" << result.revAStarTime << "s, " << result.revAStarNodeExpanded << "n)\n" << std::flush;
    }

    if (!gRunFullBaselines) {
      MarkFullBaselineSkipped(result, i);
    } else {
      std::cout << "[" << i << "] Running Pancake BAE*..." << std::flush;
      {
        BAE<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, PancakePuzzle<PANCAKE_SIZE>> bae;
        std::vector<PancakePuzzleState<PANCAKE_SIZE>> path;
        try {
          t.StartTimer();
          bae.GetPath(&pancake, puzzle, goal, &pancake, &pancake, path);
          t.EndTimer();
        }
        catch (const std::bad_alloc&) {
          t.EndTimer();
          printf("BAE* ran out of memory\n");
        }
        result.baeTime = t.GetElapsedTime();
        result.baeNodeExpanded = bae.GetNodesExpanded();
        result.solutionLength = static_cast<int>(path.size()) - 1;
        std::cout << " done (" << result.baeTime << "s, " << result.baeNodeExpanded << "n)\n" << std::flush;
      }

      std::cout << "[" << i << "] Running Pancake NBS..." << std::flush;
      {
        NBS<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, PancakePuzzle<PANCAKE_SIZE>> nbs;
        std::vector<PancakePuzzleState<PANCAKE_SIZE>> path;
        try {
          t.StartTimer();
          nbs.GetPath(&pancake, puzzle, goal, &pancake, &pancake, path);
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
    }

    std::cout << "[" << i << "] Running Pancake MM..." << std::flush;
    {
      MM<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, PancakePuzzle<PANCAKE_SIZE>> mm;
      std::vector<PancakePuzzleState<PANCAKE_SIZE>> path;
      try {
        t.StartTimer();
        mm.GetPath(&pancake, puzzle, goal, &pancake, &pancake, path);
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
      if (minSize == 0 || mmSize < minSize)
        minSize = mmSize;
      frontierSize = mm.GetNumForwardItems();
      std::cout << " done (" << result.mmTime << "s, " << result.mmNodeExpanded << "n)\n" << std::flush;
    }

    if (gRunFullBaselines) {
      std::cout << "[" << i << "] Running Pancake IDA*..." << std::flush;
      {
        IDAStar<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, false> ida;
        std::vector<PancakePuzzleState<PANCAKE_SIZE>> path;
        try {
          t.StartTimer();
          ida.GetPath(&pancake, puzzle, goal, path);
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

      std::cout << "[" << i << "] Running Pancake Rev-IDA*..." << std::flush;
      {
        IDAStar<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, false> ida;
        std::vector<PancakePuzzleState<PANCAKE_SIZE>> path;
        try {
          t.StartTimer();
          ida.GetPath(&pancake, goal, puzzle, path);
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

      std::cout << "[" << i << "] Running Pancake Parallel IDA*..." << std::flush;
      {
        ParallelIDAStar<PancakePuzzle<PANCAKE_SIZE>, PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction> ida;
        std::vector<PancakePuzzleAction> path;
        try {
          t.StartTimer();
          ida.GetPath(&pancake, puzzle, goal, path);
          t.EndTimer();
        }
        catch (const std::bad_alloc&) {
          t.EndTimer();
          printf("Parallel IDA* ran out of memory\n");
        }
        result.parallelIdaTime = t.GetElapsedTime();
        result.parallelIdaNodeExpanded = ida.GetNodesExpanded();
        result.solutionLength = static_cast<int>(path.size());
        std::cout << " done (" << result.parallelIdaTime << "s, " << result.parallelIdaNodeExpanded << "n)\n" << std::flush;
      }
    }

    double maxBaselineTime = std::max({result.aStarTime, result.revAStarTime, result.baeTime, result.nbsTime,
                                       result.mmTime, result.idaTime, result.revIdaTime, result.parallelIdaTime});
    double bihsTimeLimit = std::max(maxBaselineTime * 20.0, 120.0);
    if (frontierSize == 0) {
      frontierSize = std::max<size_t>(1, minSize);
      std::cout << "[" << i << "] MM frontier unavailable; using minSize fallback for BiHS k estimate: "
                << frontierSize << "\n" << std::flush;
    }
    unsigned long idthsStatesQuantityBound = minSize > 0
        ? static_cast<unsigned long>(minSize)
        : IDTHS_DEFAULT_STATES_BOUND;
    std::cout << "[" << i << "] Pancake BiHS-Bloom timeout limit: " << bihsTimeLimit << "s\n" << std::flush;
    std::cout << "[" << i << "] Pancake IDTHSwTrans state bound baseline: "
              << idthsStatesQuantityBound << "\n" << std::flush;

    using PancakeBiHSBloom = BiHSBloom<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, PancakePuzzle<PANCAKE_SIZE>>;

    for(int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
    {
      const BiHSRunConfig &run = BIHS_RUNS[runIdx];
      double ratio = run.ratio;
      int size_in_KiB = std::max(1, static_cast<int>(
          std::round((static_cast<double>(minSize) * get_state_size(puzzle) / 8192.0) * ratio)));

      double bloomBits = size_in_KiB * 8192.0;
      double estimatedFrontierItems = std::max(1.0, static_cast<double>(frontierSize));
      int k_hashes = ComputeKHashes(run, estimatedFrontierItems, bloomBits);
      double fp_est = fp_rate(k_hashes, estimatedFrontierItems, bloomBits);

      std::cout << "[" << i << "] Running Pancake BiHS-Bloom(" << run.ratioLabel << ", " << run.kMode
                << ", " << run.splitMode
                << ", size=" << size_in_KiB << "KiB, k=" << k_hashes << ", fp_est=" << fp_est
                << ", limit=" << bihsTimeLimit << "s)..." << std::flush;
      PancakeBiHSBloom bihs(size_in_KiB, k_hashes, bihsTimeLimit);

      bool bihsOutOfMemory = false;
      std::vector<PancakePuzzleAction> pathBiHS;
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
      result.bihsTime[runIdx] = bihsOutOfMemory ? -2.0 : (bihs.hasTimedOut() ? -1.0 : t.GetElapsedTime());
      result.bihsNodeExpanded[runIdx] = bihs.GetTotalNodesExpanded();

      bool converged = !bihsOutOfMemory && !bihs.hasTimedOut() && !pathBiHS.empty();
      if (bihsOutOfMemory)
      std::cout << " OUT OF MEMORY (" << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;

      else if (!converged)
        std::cout << " TIMED OUT (" << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;
      else
        std::cout << " done (" << result.bihsTime[runIdx] << "s, " << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;

      if (WRITE_BIHS_PARAM_LOG) {
        std::lock_guard<std::mutex> lk(logMutex);
        log << "BIHS_PARAM," << i << "," << ratio << "," << size_in_KiB << "," << k_hashes << ","
            << result.bihsTime[runIdx] << "," << result.bihsNodeExpanded[runIdx] << ","
            << fp_est << "," << converged << "," << run.kMode << "," << run.splitMode << "\n";
      }
      if (WRITE_CONVERGENCE_LOG) {
        std::lock_guard<std::mutex> lk(convMutex);
        for (const auto &s : bihs.GetIterStats())
          convLog << i << "," << size_in_KiB << "," << ratio << ","
                  << s.totalDepth << "," << s.iteration << ","
                  << s.nInserted << "," << s.nUnique << "," << s.estimatedFP << ","
                  << s.bitsSet << "," << s.fillRatio << "," << s.expectedFillRatio << ","
                  << s.materializedForwardStates << "," << s.materializedBackwardStates << ","
                  << s.materializedTotalStates << ","
                  << (s.isTypeSplit ? "type" : "iter") << ","
                  << s.typeIndex << "," << s.typeCount << ","
                  << s.boundCycle << "," << s.forwardDepth << "," << s.backwardDepth << ","
                  << s.cumulativeForwardBloomScans << "," << s.cumulativeBackwardBloomScans << ","
                  << s.cumulativeTypeSplitScans << "," << s.firstForwardWork << ","
                  << s.firstBackwardWork << "," << s.observedNextF << ","
                  << run.kMode << "," << k_hashes << "," << run.splitMode << "\n";
        convLog.flush();
      }
      if (converged)
        result.solutionLength = static_cast<int>(pathBiHS.size());

      unsigned long idthsStorage = std::max<unsigned long>(
          static_cast<unsigned long>(idthsStatesQuantityBound * run.ratio),
          IDTHS_MIN_STATES_BOUND);
      result.idthsTransStorage[runIdx] = idthsStorage;

      std::cout << "[" << i << "] Running Pancake IDTHSwTrans(" << run.ratioLabel
                << ", states=" << idthsStorage << ", limit=" << IDTHS_SECONDS_LIMIT << "s)..."
                << std::flush;

      IDTHSwTrans<PancakePuzzleState<PANCAKE_SIZE>, PancakePuzzleAction, false> idthsTrans(true, true, true, 1, false);
      bool idthsSolved = false;
      bool idthsOutOfMemory = false;
      try {
        t.StartTimer();
        idthsSolved = idthsTrans.GetPath(&pancake, puzzle, goal, IDTHS_SECONDS_LIMIT, idthsStorage);
        t.EndTimer();
      }
      catch (const std::bad_alloc&) {
        t.EndTimer();
        idthsOutOfMemory = true;
        printf("IDTHSwTrans ran out of memory\n");
      }

      result.idthsTransTime[runIdx] = idthsOutOfMemory ? -2.0 : (idthsSolved ? t.GetElapsedTime() : -1.0);
      result.idthsTransNodeExpanded[runIdx] = idthsTrans.GetNodesExpanded();
      result.idthsTransNecessaryExpanded[runIdx] = idthsTrans.GetNecessaryExpansions();

      if (idthsOutOfMemory)
        std::cout << " OUT OF MEMORY (" << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
      else if (!idthsSolved)
        std::cout << " TIMED OUT (" << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
      else {
        result.solutionLength = static_cast<int>(idthsTrans.getPathLength());
        std::cout << " done (" << result.idthsTransTime[runIdx] << "s, "
                  << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
      }
    }

    std::cout << "Pancake #" << result.instance
              << " | A*: " << result.aStarTime << "s/" << result.aStarNodeExpanded << "n"
              << " | Rev-A*: " << result.revAStarTime << "s/" << result.revAStarNodeExpanded << "n"
              << " | BAE*: " << result.baeTime << "s/" << result.baeNodeExpanded << "n"
              << " | NBS: " << result.nbsTime << "s/" << result.nbsNodeExpanded << "n"
              << " | MM: " << result.mmTime << "s/" << result.mmNodeExpanded << "n"
              << " | IDA*: " << result.idaTime << "s/" << result.idaNodeExpanded << "n"
              << " | Parallel IDA*: " << result.parallelIdaTime << "s/" << result.parallelIdaNodeExpanded << "n"
              << " | Rev-IDA*: " << result.revIdaTime << "s/" << result.revIdaNodeExpanded << "n";
    for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
      const auto &run = BIHS_RUNS[runIdx];
      std::cout << " | BiHS-Bloom(" << run.ratioLabel << "," << run.kMode << "," << run.splitMode
                << "): " << result.bihsTime[runIdx] << "s/" << result.bihsNodeExpanded[runIdx] << "n";
      std::cout << " | IDTHSwTrans(" << run.ratioLabel
                << "): " << result.idthsTransTime[runIdx] << "s/"
                << result.idthsTransNodeExpanded[runIdx] << "n";
    }
    std::cout << " | Length: " << result.solutionLength << std::endl;

    {
      std::lock_guard<std::mutex> lk(logMutex);
      log << result.instance << "," << result.solutionLength << ","
          << result.aStarTime << ","
          << result.revAStarTime << ","
          << result.baeTime << ","
          << result.nbsTime << ","
          << result.mmTime << ","
          << result.idaTime << ","
          << result.parallelIdaTime << ","
          << result.revIdaTime << ","
          << result.aStarNodeExpanded << "," << result.revAStarNodeExpanded << ","
          << result.baeNodeExpanded << "," << result.nbsNodeExpanded << "," << result.mmNodeExpanded << ","
          << result.idaNodeExpanded << "," << result.parallelIdaNodeExpanded << "," << result.revIdaNodeExpanded << ","
          << result.bihsTime[0];
      for (int runIdx = 1; runIdx < NUM_BIHS_RUNS; ++runIdx) {
        log << "," << result.bihsTime[runIdx];
      }
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
        log << "," << result.bihsNodeExpanded[runIdx];
      }
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
        log << "," << result.idthsTransTime[runIdx];
      }
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
        log << "," << result.idthsTransNodeExpanded[runIdx];
      }
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
        log << "," << result.idthsTransNecessaryExpanded[runIdx];
      }
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
        log << "," << result.idthsTransStorage[runIdx];
      }
      log << "\n";
      log.flush();
    }
  }

  log.close();
  std::cout << "Results written to " << benchmarkFile << std::endl;
}

void solveRubik(int instanceStart, int instanceEnd, const std::string &benchmarkFile,
                const std::string &convergenceFile){
  std::ofstream log(benchmarkFile);
  std::vector<std::string> headers = {"instance", "solution_length",
      "a_star_time", "rev_a_star_time", "bae_time", "nbs_time", "mm_time", "ida_time", "parallel_ida_time", "rev_ida_time",
      "a_star_nodes", "rev_a_star_nodes", "bae_nodes", "nbs_nodes", "mm_nodes", "ida_nodes", "parallel_ida_nodes", "rev_ida_nodes",
      };
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("bihs_bloom_time_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("bihs_bloom_nodes_") + run.ratioSlug + "_" + run.kMode + "_" + run.splitMode);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_trans_time_") + run.ratioSlug);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_trans_nodes_") + run.ratioSlug);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_trans_necessary_nodes_") + run.ratioSlug);
  for (const auto &run : BIHS_RUNS)
    headers.push_back(std::string("idths_trans_storage_states_") + run.ratioSlug);
  for(size_t i = 0; i < headers.size(); ++i) {
    log << headers[i];
    if (i < headers.size() - 1) log << ",";
  }
  log << "\n";

  std::ofstream convLog;
  if (WRITE_CONVERGENCE_LOG) {
    convLog.open(convergenceFile);
    convLog << CONVERGENCE_HEADER;
  }

  std::mutex logMutex;
  std::mutex convMutex;

  for (int i = instanceStart; i < instanceEnd; i++) {
    STPResult result;
    result.instance = i;
    result.solutionLength = -1;
    result.aStarTime = -1;
    result.revAStarTime = -1;
    result.baeTime = -1;
    result.mmTime = -1;
    result.nbsTime = -1;
    result.idaTime = -1;
    result.parallelIdaTime = -1;
    result.revIdaTime = -1;
    result.bihsTime.fill(-1);
    result.idthsTransTime.fill(-1);
    result.bihsNodeExpanded.fill(0);
    result.idthsTransNodeExpanded.fill(0);
    result.idthsTransNecessaryExpanded.fill(0);
    result.idthsTransStorage.fill(0);
    result.aStarNodeExpanded = 0;
    result.revAStarNodeExpanded = 0;
    result.baeNodeExpanded = 0;
    result.mmNodeExpanded = 0;
    result.nbsNodeExpanded = 0;
    result.idaNodeExpanded = 0;
    result.parallelIdaNodeExpanded = 0;
    result.revIdaNodeExpanded = 0;

    size_t minSize = 0;
    size_t frontierSize = 0;

    BenchmarkRC rubik;
    RubiksState goal;
    RubiksState puzzle;
    goal.Reset();
    RubiksCubeInstances::GetKorfRubikInstance(puzzle, i);
    Timer t;

    std::cout << "[" << i << "] Running Rubik A*..." << std::flush;
    {
      TemplateAStar<RubiksState, RubiksAction, BenchmarkRC> astar;
      std::vector<RubiksState> path;
      try {
        t.StartTimer();
        astar.GetPath(&rubik, puzzle, goal, path);
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

    std::cout << "[" << i << "] Running Rubik Rev-A*..." << std::flush;
    {
      TemplateAStar<RubiksState, RubiksAction, BenchmarkRC> astar;
      std::vector<RubiksState> path;
      try {
        t.StartTimer();
        astar.GetPath(&rubik, goal, puzzle, path);
        t.EndTimer();
      }
      catch (const std::bad_alloc&) {
        t.EndTimer();
        printf("Rev-A* ran out of memory\n");
      }
      result.revAStarTime = t.GetElapsedTime();
      result.revAStarNodeExpanded = astar.GetNodesExpanded();
      result.solutionLength = static_cast<int>(path.size()) - 1;
      if (minSize == 0 || astar.GetNumItems() < minSize)
        minSize = astar.GetNumItems();
      std::cout << " done (" << result.revAStarTime << "s, " << result.revAStarNodeExpanded << "n)\n" << std::flush;
    }

    if (!gRunFullBaselines) {
      MarkFullBaselineSkipped(result, i);
    } else {
      std::cout << "[" << i << "] Running Rubik BAE*..." << std::flush;
      {
        BAE<RubiksState, RubiksAction, BenchmarkRC> bae;
        std::vector<RubiksState> path;
        try {
          t.StartTimer();
          bae.GetPath(&rubik, puzzle, goal, &rubik, &rubik, path);
          t.EndTimer();
        }
        catch (const std::bad_alloc&) {
          t.EndTimer();
          printf("BAE* ran out of memory\n");
        }
        result.baeTime = t.GetElapsedTime();
        result.baeNodeExpanded = bae.GetNodesExpanded();
        result.solutionLength = static_cast<int>(path.size()) - 1;
        std::cout << " done (" << result.baeTime << "s, " << result.baeNodeExpanded << "n)\n" << std::flush;
      }

      std::cout << "[" << i << "] Running Rubik NBS..." << std::flush;
      {
        NBS<RubiksState, RubiksAction, BenchmarkRC> nbs;
        std::vector<RubiksState> path;
        try {
          t.StartTimer();
          nbs.GetPath(&rubik, puzzle, goal, &rubik, &rubik, path);
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
    }

    std::cout << "[" << i << "] Running Rubik MM..." << std::flush;
    {
      MM<RubiksState, RubiksAction, BenchmarkRC> mm;
      std::vector<RubiksState> path;
      try {
        t.StartTimer();
        mm.GetPath(&rubik, puzzle, goal, &rubik, &rubik, path);
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
      if (minSize == 0 || mmSize < minSize)
        minSize = mmSize;
      frontierSize = mm.GetNumForwardItems();
      std::cout << " done (" << result.mmTime << "s, " << result.mmNodeExpanded << "n)\n" << std::flush;
    }

    if (gRunFullBaselines) {
      std::cout << "[" << i << "] Running Rubik IDA*..." << std::flush;
      {
        IDAStar<RubiksState, RubiksAction, false> ida;
        std::vector<RubiksState> path;
        try {
          t.StartTimer();
          ida.GetPath(&rubik, puzzle, goal, path);
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

      std::cout << "[" << i << "] Running Rubik Rev-IDA*..." << std::flush;
      {
        IDAStar<RubiksState, RubiksAction, false> ida;
        std::vector<RubiksState> path;
        try {
          t.StartTimer();
          ida.GetPath(&rubik, goal, puzzle, path);
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

      std::cout << "[" << i << "] Running Rubik Parallel IDA*..." << std::flush;
      {
        ParallelIDAStar<BenchmarkRC, RubiksState, RubiksAction> ida;
        std::vector<RubiksAction> path;
        try {
          t.StartTimer();
          ida.GetPath(&rubik, puzzle, goal, path);
          t.EndTimer();
        }
        catch (const std::bad_alloc&) {
          t.EndTimer();
          printf("Parallel IDA* ran out of memory\n");
        }
        result.parallelIdaTime = t.GetElapsedTime();
        result.parallelIdaNodeExpanded = ida.GetNodesExpanded();
        result.solutionLength = static_cast<int>(path.size());
        std::cout << " done (" << result.parallelIdaTime << "s, " << result.parallelIdaNodeExpanded << "n)\n" << std::flush;
      }
    }

    double maxBaselineTime = std::max({result.aStarTime, result.revAStarTime, result.baeTime, result.nbsTime,
                                       result.mmTime, result.idaTime, result.revIdaTime, result.parallelIdaTime});
    double bihsTimeLimit = std::max(maxBaselineTime * 20.0, 120.0);
    if (frontierSize == 0)
      frontierSize = std::max<size_t>(1, minSize);
    unsigned long idthsStatesQuantityBound = minSize > 0
        ? static_cast<unsigned long>(minSize)
        : IDTHS_DEFAULT_STATES_BOUND;
    std::cout << "[" << i << "] Rubik BiHS-Bloom timeout limit: " << bihsTimeLimit << "s\n" << std::flush;
    std::cout << "[" << i << "] Rubik IDTHSwTrans state bound baseline: "
              << idthsStatesQuantityBound << "\n" << std::flush;

    using RubikBiHSBloom = BiHSBloom<RubiksState, RubiksAction, BenchmarkRC>;

    for(int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
    {
      const BiHSRunConfig &run = BIHS_RUNS[runIdx];
      double ratio = run.ratio;
      int size_in_KiB = std::max(1, static_cast<int>(
          std::round((static_cast<double>(std::max<size_t>(1, minSize)) * get_state_size(puzzle) / 8192.0) * ratio)));

      double bloomBits = size_in_KiB * 8192.0;
      double estimatedFrontierItems = std::max(1.0, static_cast<double>(frontierSize));
      int k_hashes = ComputeKHashes(run, estimatedFrontierItems, bloomBits);
      double fp_est = fp_rate(k_hashes, estimatedFrontierItems, bloomBits);

      std::cout << "[" << i << "] Running Rubik BiHS-Bloom(" << run.ratioLabel << ", " << run.kMode
                << ", " << run.splitMode
                << ", size=" << size_in_KiB << "KiB, k=" << k_hashes << ", fp_est=" << fp_est
                << ", limit=" << bihsTimeLimit << "s)..." << std::flush;

      RubikBiHSBloom bihs(size_in_KiB, k_hashes, bihsTimeLimit);
      bool bihsOutOfMemory = false;
      std::vector<RubiksAction> pathBiHS;
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

      result.bihsTime[runIdx] = bihsOutOfMemory ? -2.0 : (bihs.hasTimedOut() ? -1.0 : t.GetElapsedTime());
      result.bihsNodeExpanded[runIdx] = bihs.GetTotalNodesExpanded();
      bool converged = !bihsOutOfMemory && !bihs.hasTimedOut() && !pathBiHS.empty();

      if (bihsOutOfMemory)
        std::cout << " OUT OF MEMORY (" << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;
      else if (!converged)
        std::cout << " TIMED OUT (" << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;
      else
        std::cout << " done (" << result.bihsTime[runIdx] << "s, " << result.bihsNodeExpanded[runIdx] << "n)\n" << std::flush;

      if (WRITE_BIHS_PARAM_LOG) {
        std::lock_guard<std::mutex> lk(logMutex);
        log << "BIHS_PARAM," << i << "," << ratio << "," << size_in_KiB << "," << k_hashes << ","
            << result.bihsTime[runIdx] << "," << result.bihsNodeExpanded[runIdx] << ","
            << fp_est << "," << converged << "," << run.kMode << "," << run.splitMode << "\n";
      }
      if (WRITE_CONVERGENCE_LOG) {
        std::lock_guard<std::mutex> lk(convMutex);
        for (const auto &s : bihs.GetIterStats())
          convLog << i << "," << size_in_KiB << "," << ratio << ","
                  << s.totalDepth << "," << s.iteration << ","
                  << s.nInserted << "," << s.nUnique << "," << s.estimatedFP << ","
                  << s.bitsSet << "," << s.fillRatio << "," << s.expectedFillRatio << ","
                  << s.materializedForwardStates << "," << s.materializedBackwardStates << ","
                  << s.materializedTotalStates << ","
                  << (s.isTypeSplit ? "type" : "iter") << ","
                  << s.typeIndex << "," << s.typeCount << ","
                  << s.boundCycle << "," << s.forwardDepth << "," << s.backwardDepth << ","
                  << s.cumulativeForwardBloomScans << "," << s.cumulativeBackwardBloomScans << ","
                  << s.cumulativeTypeSplitScans << "," << s.firstForwardWork << ","
                  << s.firstBackwardWork << "," << s.observedNextF << ","
                  << run.kMode << "," << k_hashes << "," << run.splitMode << "\n";
        convLog.flush();
      }
      if (converged)
        result.solutionLength = static_cast<int>(pathBiHS.size());

      unsigned long idthsStorage = std::max<unsigned long>(
          static_cast<unsigned long>(idthsStatesQuantityBound * run.ratio),
          IDTHS_MIN_STATES_BOUND);
      result.idthsTransStorage[runIdx] = idthsStorage;

      std::cout << "[" << i << "] Running Rubik IDTHSwTrans(" << run.ratioLabel
                << ", states=" << idthsStorage << ", limit=" << IDTHS_SECONDS_LIMIT << "s)..."
                << std::flush;

      IDTHSwTrans<RubiksState, RubiksAction, false> idthsTrans(true, true, true, 1, true);
      bool idthsSolved = false;
      bool idthsOutOfMemory = false;
      try {
        t.StartTimer();
        idthsSolved = idthsTrans.GetPath(&rubik, puzzle, goal, IDTHS_SECONDS_LIMIT, idthsStorage);
        t.EndTimer();
      }
      catch (const std::bad_alloc&) {
        t.EndTimer();
        idthsOutOfMemory = true;
        printf("IDTHSwTrans ran out of memory\n");
      }

      result.idthsTransTime[runIdx] = idthsOutOfMemory ? -2.0 : (idthsSolved ? t.GetElapsedTime() : -1.0);
      result.idthsTransNodeExpanded[runIdx] = idthsTrans.GetNodesExpanded();
      result.idthsTransNecessaryExpanded[runIdx] = idthsTrans.GetNecessaryExpansions();

      if (idthsOutOfMemory)
        std::cout << " OUT OF MEMORY (" << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
      else if (!idthsSolved)
        std::cout << " TIMED OUT (" << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
      else {
        result.solutionLength = static_cast<int>(idthsTrans.getPathLength());
        std::cout << " done (" << result.idthsTransTime[runIdx] << "s, "
                  << result.idthsTransNodeExpanded[runIdx] << "n)\n" << std::flush;
      }
    }

    std::cout << "Rubik #" << result.instance
              << " | A*: " << result.aStarTime << "s/" << result.aStarNodeExpanded << "n"
              << " | Rev-A*: " << result.revAStarTime << "s/" << result.revAStarNodeExpanded << "n"
              << " | BAE*: " << result.baeTime << "s/" << result.baeNodeExpanded << "n"
              << " | NBS: " << result.nbsTime << "s/" << result.nbsNodeExpanded << "n"
              << " | MM: " << result.mmTime << "s/" << result.mmNodeExpanded << "n"
              << " | IDA*: " << result.idaTime << "s/" << result.idaNodeExpanded << "n"
              << " | Parallel IDA*: " << result.parallelIdaTime << "s/" << result.parallelIdaNodeExpanded << "n"
              << " | Rev-IDA*: " << result.revIdaTime << "s/" << result.revIdaNodeExpanded << "n";
    for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx) {
      const auto &run = BIHS_RUNS[runIdx];
      std::cout << " | BiHS-Bloom(" << run.ratioLabel << "," << run.kMode << "," << run.splitMode
                << "): " << result.bihsTime[runIdx] << "s/" << result.bihsNodeExpanded[runIdx] << "n";
      std::cout << " | IDTHSwTrans(" << run.ratioLabel
                << "): " << result.idthsTransTime[runIdx] << "s/"
                << result.idthsTransNodeExpanded[runIdx] << "n";
    }
    std::cout << " | Length: " << result.solutionLength << std::endl;

    {
      std::lock_guard<std::mutex> lk(logMutex);
      log << result.instance << "," << result.solutionLength << ","
          << result.aStarTime << ","
          << result.revAStarTime << ","
          << result.baeTime << ","
          << result.nbsTime << ","
          << result.mmTime << ","
          << result.idaTime << ","
          << result.parallelIdaTime << ","
          << result.revIdaTime << ","
          << result.aStarNodeExpanded << "," << result.revAStarNodeExpanded << ","
          << result.baeNodeExpanded << "," << result.nbsNodeExpanded << "," << result.mmNodeExpanded << ","
          << result.idaNodeExpanded << "," << result.parallelIdaNodeExpanded << "," << result.revIdaNodeExpanded << ","
          << result.bihsTime[0];
      for (int runIdx = 1; runIdx < NUM_BIHS_RUNS; ++runIdx)
        log << "," << result.bihsTime[runIdx];
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
        log << "," << result.bihsNodeExpanded[runIdx];
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
        log << "," << result.idthsTransTime[runIdx];
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
        log << "," << result.idthsTransNodeExpanded[runIdx];
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
        log << "," << result.idthsTransNecessaryExpanded[runIdx];
      for (int runIdx = 0; runIdx < NUM_BIHS_RUNS; ++runIdx)
        log << "," << result.idthsTransStorage[runIdx];
      log << "\n";
      log.flush();
    }
  }

  log.close();
  std::cout << "Results written to " << benchmarkFile << std::endl;
}


#ifndef STP_BIHS_BLOOM_TEST
int main(int argc, char **argv) {
  bool slidingTilePuzzle = false;
  bool pancake = false;
  bool rubik = false;
  bool prepareRubikPDBs = false;
  int instanceStart = 0;
  int instanceEnd = 100;
  int singleInstance = -1;
  double ratio = 0.0;
  std::string phase;
  std::string algorithm;
  std::string kMode;
  std::string paramsFile = "results/split/params/stp_params.csv";
  std::string benchmarkFile = "benchmark_stp_korf100.csv";
  std::string convergenceFile = "bloom_convergence.csv";

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--stp") == 0 )
      slidingTilePuzzle = true;
    else if (strcmp(argv[i], "--pancake") == 0)
      pancake = true;
    else if (strcmp(argv[i], "--rubik") == 0)
      rubik = true;
    else if (strcmp(argv[i], "--prepare-rubik-pdbs") == 0) {
      prepareRubikPDBs = true;
      gBuildRubikPDBs = true;
    }
    else if (strcmp(argv[i], "--rubik-pdb-dir") == 0 && i + 1 < argc)
      gRubikPDBDir = argv[++i];
    else if (strcmp(argv[i], "--full") == 0)
      gRunFullBaselines = true;
    else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc)
      phase = argv[++i];
    else if (strcmp(argv[i], "--algorithm") == 0 && i + 1 < argc)
      algorithm = argv[++i];
    else if (strcmp(argv[i], "--instance") == 0 && i + 1 < argc)
      singleInstance = std::stoi(argv[++i]);
    else if (strcmp(argv[i], "--ratio") == 0 && i + 1 < argc)
      ratio = std::stod(argv[++i]);
    else if (strcmp(argv[i], "--k-mode") == 0 && i + 1 < argc)
      kMode = argv[++i];
    else if (strcmp(argv[i], "--params-input") == 0 && i + 1 < argc)
      paramsFile = argv[++i];
    else if (strcmp(argv[i], "--instance-start") == 0 && i + 1 < argc)
      instanceStart = std::stoi(argv[++i]);
    else if (strcmp(argv[i], "--instance-end") == 0 && i + 1 < argc)
      instanceEnd = std::stoi(argv[++i]);
    else if (strcmp(argv[i], "--benchmark-output") == 0 && i + 1 < argc)
      benchmarkFile = argv[++i];
    else if (strcmp(argv[i], "--convergence-output") == 0 && i + 1 < argc)
      convergenceFile = argv[++i];
  }

  
  if ((pancake ? 1 : 0) + (slidingTilePuzzle ? 1 : 0) + (rubik ? 1 : 0) != 1){
    std::cerr << "Please choose exactly 1 domain";
    return 0;
  }

  if (prepareRubikPDBs) {
    if (!rubik) {
      std::cerr << "--prepare-rubik-pdbs requires --rubik\n";
      return 1;
    }
    try {
      BenchmarkRC prepared;
      std::cout << "Rubik PDBs are ready in " << gRubikPDBDir << "\n";
    }
    catch (const std::exception &e) {
      std::cerr << e.what() << "\n";
      return 1;
    }
    return 0;
  }

  if (!phase.empty()) {
    if (!slidingTilePuzzle && !rubik) {
      std::cerr << "Split phase mode currently supports --stp and --rubik\n";
      return 1;
    }
    int maxInstances = slidingTilePuzzle ? 100 : RUBIK_TOTAL_INSTANCES;
    if (singleInstance < 0 || singleInstance >= maxInstances) {
      std::cerr << "Split phase mode requires --instance in range 0.." << (maxInstances - 1) << "\n";
      return 1;
    }
    if (algorithm.empty()) {
      std::cerr << "Split phase mode requires --algorithm\n";
      return 1;
    }
    if (rubik && paramsFile == "results/split/params/stp_params.csv")
      paramsFile = "results/split/params/rubik_params.csv";
    try {
      if (phase == "calibrate") {
        if (slidingTilePuzzle)
          runSTPCalibrationJob(singleInstance, algorithm, benchmarkFile);
        else
          runRubikCalibrationJob(singleInstance, algorithm, benchmarkFile);
      } else if (phase == "run") {
        if (slidingTilePuzzle)
          runSTPSplitAlgorithmJob(singleInstance, algorithm, ratio, kMode, paramsFile,
                                  benchmarkFile, convergenceFile);
        else
          runRubikSplitAlgorithmJob(singleInstance, algorithm, ratio, kMode, paramsFile,
                                    benchmarkFile, convergenceFile);
      } else if (phase == "fixed-run") {
        if (!rubik) {
          std::cerr << "Fixed-run phase currently supports --rubik only\n";
          return 1;
        }
        runRubikFixedAlgorithmJob(singleInstance, algorithm, benchmarkFile, convergenceFile);
      } else {
        std::cerr << "Unknown phase: " << phase << "\n";
        return 1;
      }
    }
    catch (const std::exception &e) {
      std::cerr << e.what() << "\n";
      return 1;
    }
    return 0;
  }

  if (slidingTilePuzzle) {
    if (instanceStart < 0 || instanceEnd > 100 || instanceStart >= instanceEnd) {
      std::cerr << "STP instance range must satisfy 0 <= start < end <= 100\n";
      return 1;
    }
    std::cout << "Domain: Sliding Tile Puzzle" << std::endl;
    solveSTP(instanceStart, instanceEnd, benchmarkFile, convergenceFile);
  }
  else if (pancake) {
    if (benchmarkFile == "benchmark_stp_korf100.csv")
      benchmarkFile = "benchmark_pancake" + std::to_string(PANCAKE_SIZE) + "_100.csv";
    if (convergenceFile == "bloom_convergence.csv")
      convergenceFile = "bloom_convergence_pancake" + std::to_string(PANCAKE_SIZE) + ".csv";
    if (instanceStart < 0 || instanceEnd > 100 || instanceStart >= instanceEnd) {
      std::cerr << "Pancake instance range must satisfy 0 <= start < end <= 100\n";
      return 1;
    }
    std::cout << "Domain: Pancake Puzzle" << std::endl;
    solvePancake(instanceStart, instanceEnd, benchmarkFile, convergenceFile);
  }
  else if (rubik) {
    if (instanceStart < 0 || instanceEnd > RUBIK_TOTAL_INSTANCES || instanceStart >= instanceEnd) {
      std::cerr << "Rubik instance range must satisfy 0 <= start < end <= "
                << RUBIK_TOTAL_INSTANCES << "\n";
      return 1;
    }
    if (benchmarkFile == "benchmark_stp_korf100.csv")
      benchmarkFile = "benchmark_rubik_" + std::string(RUBIK_INSTANCE_SET) + "_" +
                      std::to_string(RUBIK_TOTAL_INSTANCES) + ".csv";
    if (convergenceFile == "bloom_convergence.csv")
      convergenceFile = "bloom_convergence_rubik_" + std::string(RUBIK_INSTANCE_SET) + ".csv";
    std::cout << "Domain: Rubik Cube" << std::endl;
    solveRubik(instanceStart, instanceEnd, benchmarkFile, convergenceFile);
  }
} 
#endif
