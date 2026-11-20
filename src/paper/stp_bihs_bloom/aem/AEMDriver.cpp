#include "MNPuzzle.h"
#include "MM.h"
#include "PancakePuzzle.h"
#include "PancakeInstances.h"
#include "STPInstances.h"
#include "TemplateAStar.h"
#include "TOH.h"
#include "../../fresh_bytes_20261016/StrongRubik.h"
#include "RubiksInstances.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// A 4-peg N-disk state has exactly 4^N placements, hence 2N information bits.
// This overload must be visible before BiHSBloom is instantiated.
template <int N>
size_t get_state_size(const TOHState<N>&)
{
    return static_cast<size_t>(2 * N);
}

#include "AEMTable.h"
#include "AEMTOHCodec.h"
#include "BiHSBloomAEM.h"
#ifdef PAPER_BUCKET_BASELINE
#include "../../fresh_bytes_20261016/PaperIDTHSBucketsMetrics.h"
#else
#include "../../fresh_bytes_20261016/IDTHSwTrans.h"
#endif

namespace BiHSBloomHelper {
template <int N>
struct StateFingerprint<TOHState<N>, TOHMove> {
    static constexpr bool incremental = false;

    static uint64_t hash(const TOHState<N> &state)
    {
        // Stable, padding-independent, and one-to-one for N <= 32.
        uint64_t result = 0;
        for (int peg = 0; peg < 4; ++peg) {
            for (int index = 0; index < state.GetDiskCountOnPeg(peg); ++index) {
                const int disk = state.GetDiskOnPeg(peg, index);
                result |= static_cast<uint64_t>(peg) << (2 * (disk - 1));
            }
        }
        return result;
    }

    static uint64_t apply(uint64_t, const TOHState<N> &state, TOHMove)
    {
        return hash(state);
    }
};
}

namespace {

constexpr int kPancakeSize = 12;
constexpr int kSTPInstances = 100;
constexpr int kPancakeInstances = 100;
constexpr int kTOHInstances = 50;
constexpr int kTOHDisks = 14;

using STPState = MNPuzzleState<4, 4>;
using STPEnvironment = MNPuzzle<4, 4>;
using PancakeState = PancakePuzzleState<kPancakeSize>;
using PancakeEnvironment = PancakePuzzle<kPancakeSize>;

class GlibcRandom {
public:
    explicit GlibcRandom(uint32_t seed)
    {
        values.reserve(444);
        values.push_back(seed);
        for (int index = 1; index < 31; ++index) {
            const int64_t previous = static_cast<int32_t>(values[index - 1]);
            int64_t value = (16807 * previous) % 2147483647;
            if (value < 0) value += 2147483647;
            values.push_back(static_cast<uint32_t>(value));
        }
        values.push_back(values[0]);
        values.push_back(values[1]);
        values.push_back(values[2]);
        for (int index = 34; index < 344; ++index)
            values.push_back(values[index - 31] + values[index - 3]);
    }

    uint32_t Next()
    {
        const size_t index = values.size();
        values.push_back(values[index - 31] + values[index - 3]);
        return (values.back() >> 1) & 0x7fffffffU;
    }

private:
    std::vector<uint32_t> values;
};

PancakeState MakePancakeState(GlibcRandom &random)
{
    PancakeState state;
    for (int index = 0; index < kPancakeSize; ++index) {
        const int swapIndex = index + static_cast<int>(
            random.Next() % static_cast<uint32_t>(kPancakeSize - index));
        std::swap(state.puzzle[index], state.puzzle[swapIndex]);
    }
    return state;
}

PancakeState GetPancakeInstance(int requested, uint32_t seed)
{
    GlibcRandom random(seed);
    PancakeState result;
    for (int instance = 0; instance <= requested; ++instance)
        result = MakePancakeState(random);
    return result;
}

uint32_t GetTOHSeed(int instance)
{
    static const uint32_t first[16] = {
        52058078, 116173544, 208694125, 131936966,
        141559500, 133800745, 194246206, 50028346,
        167007978, 207116816, 163867037, 119897198,
        201847476, 210859515, 117688410, 121633885
    };
    static const uint32_t second[16] = {
        145008714, 165971878, 154717942, 218927374,
        182772845, 5808407, 19155194, 137438954,
        13143598, 124513215, 132635260, 39667704,
        2462244, 41006424, 214146208, 54305743
    };
    return first[instance & 0xF] ^ second[(instance >> 4) & 0xF];
}

template <int N>
TOHState<N> MakeTOHState(GlibcRandom &random)
{
    TOHState<N> result;
    for (int peg = 0; peg < 4; ++peg) result.counts[peg] = 0;
    for (int disk = N; disk > 0; --disk) {
        const int peg = static_cast<int>(random.Next() % 4U);
        result.disks[peg][result.counts[peg]] = static_cast<uint8_t>(disk);
        result.counts[peg]++;
    }
    return result;
}

template <int N>
std::pair<TOHState<N>, TOHState<N>> GetTOHInstance(int instance)
{
    GlibcRandom random(GetTOHSeed(instance));
    return {MakeTOHState<N>(random), MakeTOHState<N>(random)};
}

template <int N, int Large, int Small>
struct TOHPDBData {
    TOH<Large> largeEnvironment;
    TOH<Small> smallEnvironment;
    TOHPDB<Large, N, Small> towardGoalLarge;
    TOHPDB<Small, N> towardGoalSmall;
    TOHPDB<Large, N, Small> towardStartLarge;
    TOHPDB<Small, N> towardStartSmall;

    TOHPDBData(const TOHState<N> &start, const TOHState<N> &goal)
        : towardGoalLarge(&largeEnvironment, goal),
          towardGoalSmall(&smallEnvironment, goal),
          towardStartLarge(&largeEnvironment, start),
          towardStartSmall(&smallEnvironment, start)
    {
        // Each Slurm task requests one CPU. Do not let PDB construction escape
        // that allocation via hardware_concurrency().
        towardGoalLarge.BuildPDB(goal, 1, false);
        towardGoalSmall.BuildPDB(goal, 1, false);
        towardStartLarge.BuildPDB(start, 1, false);
        towardStartSmall.BuildPDB(start, 1, false);
    }
};

template <int N, int Large, int Small>
class TOHPDBEnvironment : public TOH<N> {
public:
    using State = TOHState<N>;

    TOHPDBEnvironment(const State &start, const State &goal)
        : start(start), goal(goal),
          pdb(std::make_shared<TOHPDBData<N, Large, Small>>(start, goal)) {}

    // BiHSBloom copies environments to evaluate origin-relative consistency
    // errors. Shared immutable PDB storage keeps those copies cheap and exact.
    TOHPDBEnvironment(const TOHPDBEnvironment&) = default;
    TOHPDBEnvironment& operator=(const TOHPDBEnvironment&) = default;

    double HCost(const State &from, const State &to) const override
    {
        if (to == goal) {
            return pdb->towardGoalLarge.HCost(from, to) +
                   pdb->towardGoalSmall.HCost(from, to);
        }
        if (to == start) {
            return pdb->towardStartLarge.HCost(from, to) +
                   pdb->towardStartSmall.HCost(from, to);
        }
        throw std::runtime_error("ToH PDB heuristic requested for a non-endpoint target");
    }

    bool GoalTest(const State &node, const State &target) const override
    {
        return node == target;
    }

    void StoreGoal(State&) {}

private:
    State start;
    State goal;
    std::shared_ptr<TOHPDBData<N, Large, Small>> pdb;
};

bool IsAEM(const std::string &a) {
    return a == "bihs_bloom_aem" || a == "bihs_bloom_aem_control";
}
bool IsBiHS(const std::string &a) { return a == "bihs_bloom" || IsAEM(a); }

struct Options {
    double aemAlpha = 0.5;
    bool aemAlphaSpecified = false;
    bool aemExactBatches = false;
    std::string aemOutput;
    std::string domain;
    std::string algorithm;
    std::string heuristic;
    std::string f2f = "na";
    int consistency = -1;
    int instance = -1;
    uint64_t memoryStates = 0;
    uint64_t memoryBytes = 0;
    int pancakeSize = 12;
    std::string pancakeSet = "random";
    int rubikWalkLength = -1;
    uint32_t rubikWalkSeed = 20261016;
    int kHashes = 1;
    double streamingQStop = 0.0; // Zero preserves the non-streaming default.
    bool streamingNoLimit = false;
    uint64_t bloomSeed = 20260824;
    uint32_t pancakeSeed = 2017218;
    std::string summaryOutput;
    std::string levelOutput;
    std::string scanOutput;
    std::string recordStatus;
    std::string recordError;
    std::string bucketOutput;
    std::string rubikPDBDir;
    int rubikSmokeDepth = -1;
    int idthsTimeoutSeconds = 43200;
    int bihsTimeoutSeconds = 0;
};

uint64_t ParseUInt64(const char *text, const char *flag)
{
    try { return std::stoull(text); }
    catch (...) { throw std::runtime_error(std::string("Invalid value for ") + flag); }
}

int ParseInt(const char *text, const char *flag)
{
    try { return std::stoi(text); }
    catch (...) { throw std::runtime_error(std::string("Invalid value for ") + flag); }
}

Options ParseOptions(int argc, char **argv)
{
    Options result;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto value = [&](const char *flag) -> const char* {
            if (index + 1 >= argc) throw std::runtime_error(std::string("Missing value for ") + flag);
            return argv[++index];
        };
        if (arg == "--domain") result.domain = value("--domain");
        else if (arg == "--bucket-output") result.bucketOutput = value("--bucket-output");
        else if (arg == "--rubik-pdb-dir") result.rubikPDBDir = value("--rubik-pdb-dir");
        else if (arg == "--rubik-smoke-depth") result.rubikSmokeDepth = ParseInt(value("--rubik-smoke-depth"), "--rubik-smoke-depth");
        else if (arg == "--idths-timeout-seconds") result.idthsTimeoutSeconds = ParseInt(value("--idths-timeout-seconds"), "--idths-timeout-seconds");
        else if (arg == "--bihs-timeout-seconds") result.bihsTimeoutSeconds = ParseInt(value("--bihs-timeout-seconds"), "--bihs-timeout-seconds");
        else if (arg == "--algorithm") result.algorithm = value("--algorithm");
        else if (arg == "--heuristic") result.heuristic = value("--heuristic");
        else if (arg == "--f2f") result.f2f = value("--f2f");
        else if (arg == "--consistency") result.consistency = ParseInt(value("--consistency"), "--consistency");
        else if (arg == "--instance") result.instance = ParseInt(value("--instance"), "--instance");
        else if (arg == "--memory-bytes") result.memoryBytes = ParseUInt64(value("--memory-bytes"), "--memory-bytes");
        else if (arg == "--pancake-size") result.pancakeSize = ParseInt(value("--pancake-size"), "--pancake-size");
        else if (arg == "--pancake-set") result.pancakeSet = value("--pancake-set");
        else if (arg == "--rubik-walk-length") result.rubikWalkLength = ParseInt(value("--rubik-walk-length"), "--rubik-walk-length");
        else if (arg == "--rubik-walk-seed") result.rubikWalkSeed = ParseUInt64(value("--rubik-walk-seed"), "--rubik-walk-seed");
        else if (arg == "--memory-states") result.memoryStates = ParseUInt64(value("--memory-states"), "--memory-states");
        else if (arg == "--aem-alpha") {
            result.aemAlpha = std::stod(value("--aem-alpha"));
            result.aemAlphaSpecified = true;
        }
        else if (arg == "--aem-output") result.aemOutput = value("--aem-output");
        else if (arg == "--aem-exact-batches") result.aemExactBatches = true;
        else if (arg == "--k-hashes") result.kHashes = ParseInt(value("--k-hashes"), "--k-hashes");
        else if (arg == "--streaming-no-limit") result.streamingNoLimit = true;
        else if (arg == "--streaming-q-stop") {
            result.streamingQStop = std::stod(value("--streaming-q-stop"));
            if (!(result.streamingQStop > 0 && result.streamingQStop < 1))
                throw std::runtime_error("--streaming-q-stop must be in (0,1)");
        }
        else if (arg == "--bloom-seed") result.bloomSeed = ParseUInt64(value("--bloom-seed"), "--bloom-seed");
        else if (arg == "--pancake-seed") result.pancakeSeed = static_cast<uint32_t>(ParseUInt64(value("--pancake-seed"), "--pancake-seed"));
        else if (arg == "--summary-output") result.summaryOutput = value("--summary-output");
        else if (arg == "--level-output") result.levelOutput = value("--level-output");
        else if (arg == "--scan-output") result.scanOutput = value("--scan-output");
        else if (arg == "--record-status") result.recordStatus = value("--record-status");
        else if (arg == "--record-error") result.recordError = value("--record-error");
        else throw std::runtime_error("Unknown argument: " + arg);
    }
    return result;
}

void ValidateOptions(const Options &options)
{
    if (options.pancakeSize != 12 && options.pancakeSize != 14 && options.pancakeSize != 16 && options.pancakeSize != 20)
        throw std::runtime_error("Pancake size must be 12, 14, 16 or 20");
    if (options.pancakeSet != "random" && options.pancakeSet != "composition" && options.pancakeSet != "fg2")
        throw std::runtime_error("Pancake set must be random, composition or fg2");
    if (options.pancakeSet == "composition" && options.pancakeSize != 16 && options.pancakeSize != 20)
        throw std::runtime_error("Published composition tables exist here only for 16 and 20");
    if (options.pancakeSet == "fg2" && options.instance != 0)
        throw std::runtime_error("FG2 is one diagnostic instance, id 0");
    if (options.rubikWalkLength != -1 && options.rubikWalkLength != 10 && options.rubikWalkLength != 12)
        throw std::runtime_error("Experimental Rubik walk length must be 10 or 12");
    if (options.rubikWalkLength >= 0 && options.rubikSmokeDepth >= 0)
        throw std::runtime_error("Choose experimental walks or legacy smoke scramble");
    if (options.domain != "stp" && options.domain != "pancake" && options.domain != "toh" && options.domain != "rubik")
        throw std::runtime_error("--domain must be stp, pancake, toh, or rubik");
    if (options.domain == "rubik" && (options.heuristic != "pdb-8c-6e-6e" || options.rubikPDBDir.empty()))
        throw std::runtime_error("Rubik requires --heuristic pdb-8c-6e-6e and --rubik-pdb-dir");
    if (options.rubikSmokeDepth < -1 || options.rubikSmokeDepth > 10)
        throw std::runtime_error("Rubik smoke depth must be between 0 and 10 (or omitted)");
    if (!IsBiHS(options.algorithm) && options.algorithm != "idths_trans" &&
        options.algorithm != "astar" && options.algorithm != "rev_astar" &&
        options.algorithm != "mm")
        throw std::runtime_error(
            "--algorithm must be bihs_bloom, bihs_bloom_aem, bihs_bloom_aem_control, idths_trans, astar, rev_astar, or mm");
    if (IsAEM(options.algorithm)) {
        if (!options.memoryBytes || options.aemOutput.empty() || options.bihsTimeoutSeconds <= 0)
            throw std::runtime_error("AEM requires --memory-bytes, --aem-output and a positive --bihs-timeout-seconds");
        if (!std::isfinite(options.aemAlpha) || options.aemAlpha <= 0 ||
            options.aemAlpha > 1 || (!options.aemExactBatches && options.aemAlpha == 1))
            throw std::runtime_error("--aem-alpha must be in (0,1), or (0,1] with exact batches");
        if (options.aemExactBatches && options.domain != "stp")
            throw std::runtime_error("--aem-exact-batches requires the STP domain");
        if (options.streamingNoLimit || options.streamingQStop > 0)
            throw std::runtime_error("AEM is a separate non-streaming variant");
    } else if (options.aemAlphaSpecified || options.aemExactBatches || !options.aemOutput.empty()) {
        throw std::runtime_error("AEM options require an explicit AEM algorithm label");
    }
    if (options.consistency != 0 && options.consistency != 1)
        throw std::runtime_error("--consistency must be 0 or 1");
    if ((options.memoryStates == 0) == (options.memoryBytes == 0))
        throw std::runtime_error("Specify exactly one positive --memory-states or --memory-bytes");
    if (options.kHashes <= 0)
        throw std::runtime_error("--k-hashes must be positive");
    if (options.idthsTimeoutSeconds <= 0)
        throw std::runtime_error("--idths-timeout-seconds must be positive");
    if (options.bihsTimeoutSeconds < 0)
        throw std::runtime_error("--bihs-timeout-seconds must be nonnegative");
    if (options.summaryOutput.empty() || options.levelOutput.empty() || options.scanOutput.empty())
        throw std::runtime_error("--summary-output, --level-output, and --scan-output are required");

    const int limit = options.domain == "toh" ? kTOHInstances :
                      (options.domain == "stp" ? kSTPInstances : kPancakeInstances);
    if (options.instance < 0 || options.instance >= limit)
        throw std::runtime_error("--instance is outside the benchmark range");

    if (options.domain == "stp" && options.heuristic != "manhattan")
        throw std::runtime_error("STP requires --heuristic manhattan");
    if (options.domain == "pancake" && options.heuristic != "gap" &&
        options.heuristic != "gap-1" && options.heuristic != "gap-2" && options.heuristic != "gap-3")
        throw std::runtime_error("Pancake requires --heuristic gap, gap-1, gap-2, or gap-3");
    if (options.domain == "toh" && options.heuristic != "pdb10+4" && options.heuristic != "pdb12+2")
        throw std::runtime_error("ToH requires --heuristic pdb10+4 or pdb12+2");

    const bool f2fApplicable = options.algorithm == "idths_trans" && options.domain != "toh";
    if (f2fApplicable) {
        if (options.f2f != "0" && options.f2f != "1")
            throw std::runtime_error("IDTHSwTrans STP/Pancake requires --f2f 0 or 1");
    } else if (options.f2f != "na") {
        throw std::runtime_error("F2F is not available for this algorithm/domain; use --f2f na");
    }
}

std::string EscapeCSV(const std::string &value)
{
    if (value.find_first_of(",\"\n") == std::string::npos) return value;
    std::string result = "\"";
    for (char c : value) result += (c == '\"') ? "\"\"" : std::string(1, c);
    return result + "\"";
}

struct RunResult {
    size_t aemAttempts = 0, aemOverflows = 0, aemPeakManaged = 0, actualBloomBytes = 0;
    size_t idthsPeakStored = 0;
    size_t bihsPeakExactStored = 0;
    std::string status = "failed";
    std::string error;
    double setupSeconds = 0;
    double searchSeconds = 0;
    int solutionLength = -1;
    uint64_t expanded = 0;
    uint64_t generated = 0;
    uint64_t forwardExpanded = 0;
    uint64_t forwardGenerated = 0;
    uint64_t backwardExpanded = 0;
    uint64_t backwardGenerated = 0;
    uint64_t memoryItems = 0;
    uint64_t frontierItems = 0;
    uint64_t boundCycles = 0;
    uint64_t forwardScans = 0;
    uint64_t backwardScans = 0;
    uint64_t extractionScans = 0;
    uint64_t tableFullScans = 0;
    uint64_t endCycleScans = 0;
    size_t peakInserted = 0;
    size_t peakEstimatedUnique = 0;
    size_t peakBitsSet = 0;
    double peakFillRatio = 0;
    double peakEstimatedFP = 0;
    size_t peakMaterialized = 0;
    uint64_t actualF2FPruned = 0;
    uint64_t actualConsistencyPruned = 0;
    uint64_t shadowF2FWouldPrune = 0;
    uint64_t shadowConsistencyWouldPrune = 0;
    uint64_t shadowBothWouldPrune = 0;
    std::string finalBuckets = "na";
};

template <class Search>
const char *BiHSPhaseNameGeneric(typename Search::LevelPhase phase)
{
    switch (phase) {
        case Search::LevelPhase::FRONTIER: return "frontier";
        case Search::LevelPhase::STREAMING: return "streaming";
        case Search::LevelPhase::MATERIALIZATION: return "materialization";
        case Search::LevelPhase::INTERSECTION: return "intersection";
    }
    return "unknown";
}

void WriteLevelHeader(std::ofstream &out)
{
    out << "domain,instance,algorithm,heuristic,f2f,consistency,bound_cycle,scan_ordinal,"
           "phase,direction,total_bound,direction_bound,target_depth,depth,visits,expanded,"
           "generated,accepted,frontier_inserted,base_pruned,duplicate_pruned,candidate_checks,"
           "candidates_pruned,direction_depth_pruned,actual_f2f_pruned,actual_consistency_pruned,"
           "actual_feature_overlap,shadow_f2f_would_prune,shadow_consistency_would_prune,"
           "shadow_both_would_prune,bloom_checks,bloom_rejected,counterfactual_scope\n";
}

template <class Search>
void WriteBiHSLevels(std::ofstream &out, const Options &options, const Search &search,
                     RunResult &result)
{
    for (const auto &level : search.GetLevelStats()) {
        if (level.isForward) {
            result.forwardExpanded += level.expanded;
            result.forwardGenerated += level.generated;
        } else {
            result.backwardExpanded += level.expanded;
            result.backwardGenerated += level.generated;
        }
        result.actualConsistencyPruned += level.actualConsistencyPruned;
        result.shadowConsistencyWouldPrune += level.shadowConsistencyWouldPrune;
        out << options.domain << ',' << options.instance << ",bihs_bloom,"
            << options.heuristic << ",na," << options.consistency << ','
            << level.boundCycle << ',' << level.scanOrdinal << ','
            << BiHSPhaseNameGeneric<Search>(level.phase) << ','
            << (level.isForward ? "forward" : "backward") << ','
            << level.totalBound << ",na," << level.targetDepth << ',' << level.depth << ','
            << level.visits << ',' << level.expanded << ',' << level.generated << ','
            << level.accepted << ',' << level.frontierInserted << ',' << level.basePruned
            << ",0,0,0,0,na," << level.actualConsistencyPruned << ",na,na,"
            << level.shadowConsistencyWouldPrune << ",na," << level.bloomChecks << ','
            << level.bloomRejected << ",local_node_test\n";
    }
}

template <class Search>
void WriteIDTHSLevels(std::ofstream &out, const Options &options, const Search &search,
                      bool f2fApplicable, RunResult &result)
{
    for (const auto &level : search.GetLevelStats()) {
        result.actualF2FPruned += level.actualF2FPruned;
        result.actualConsistencyPruned += level.actualConsistencyPruned;
        result.shadowF2FWouldPrune += level.shadowF2FWouldPrune;
        result.shadowConsistencyWouldPrune += level.shadowConsistencyWouldPrune;
        result.shadowBothWouldPrune += level.shadowBothWouldPrune;
        out << options.domain << ',' << options.instance << ",idths_trans,"
            << options.heuristic << ',' << options.f2f << ',' << options.consistency << ','
            << level.boundCycle << ',' << level.scanOrdinal << ",search,"
            << (level.isForward ? "forward" : "backward") << ','
            << level.totalBound << ',' << level.directionBound << ",na," << level.depth << ','
            << level.visits << ',' << level.expanded << ',' << level.generated << ','
            << level.accepted << ',' << level.frontierInserted << ',' << level.basePruned << ','
            << level.duplicatePruned << ',' << level.candidateChecks << ','
            << level.candidatesPruned << ',' << level.directionDepthPruned << ',';
        if (f2fApplicable) out << level.actualF2FPruned;
        else out << "na";
        out << ',' << level.actualConsistencyPruned << ',';
        if (f2fApplicable) out << level.actualFeatureOverlap;
        else out << "na";
        out << ',';
        if (f2fApplicable) out << level.shadowF2FWouldPrune;
        else out << "na";
        out << ',' << level.shadowConsistencyWouldPrune << ',';
        if (f2fApplicable) out << level.shadowBothWouldPrune;
        else out << "na";
        out << ",0,0,local_candidate_test\n";
    }
}

void WriteScanHeader(std::ofstream &out)
{
    out << "domain,instance,algorithm,heuristic,f2f,consistency,bound_cycle,total_bound,"
           "forward_depth,backward_depth,iteration,direction,expanded,cumulative_expanded,"
           "candidates,n_inserted,estimated_unique,bits_set,fill_ratio,estimated_fp,"
           "materialized_forward,materialized_backward,materialized_total,forward_scans,"
           "backward_scans,table_full_scans,end_cycle_scans,type_split,type_index,type_count,"
           "solved\n";
}

template <class Search>
void WriteBiHSScans(std::ofstream &out, const Options &options, const Search &search,
                    RunResult &result)
{
    for (const auto &scan : search.GetIterStats()) {
        if (search.IsStreamingEnabled())
            std::cout << "BATCH_SCAN cycle=" << scan.boundCycle << " batch=" << scan.streamBatch
                      << " iteration=" << scan.iteration << " forward=" << scan.isForwardScan
                      << " expanded=" << scan.nodesExpandedThisScan << '\n';
        result.peakInserted = std::max(result.peakInserted, scan.nInserted);
        result.peakEstimatedUnique = std::max(result.peakEstimatedUnique, scan.nUnique);
        result.peakBitsSet = std::max(result.peakBitsSet, scan.bitsSet);
        result.peakFillRatio = std::max(result.peakFillRatio, scan.fillRatio);
        result.peakEstimatedFP = std::max(result.peakEstimatedFP, scan.estimatedFP);
        result.peakMaterialized = std::max(result.peakMaterialized, scan.materializedTotalStates);
        out << options.domain << ',' << options.instance << ",bihs_bloom,"
            << options.heuristic << ",na," << options.consistency << ','
            << scan.boundCycle << ',' << scan.totalDepth << ',' << scan.forwardDepth << ','
            << scan.backwardDepth << ',' << scan.iteration << ','
            << (scan.isForwardScan ? "forward" : "backward") << ','
            << scan.nodesExpandedThisScan << ',' << scan.cumulativeNodesExpanded << ",na,"
            << scan.nInserted << ',' << scan.nUnique << ',' << scan.bitsSet << ','
            << scan.fillRatio << ',' << scan.estimatedFP << ','
            << scan.materializedForwardStates << ',' << scan.materializedBackwardStates << ','
            << scan.materializedTotalStates << ',' << scan.cumulativeForwardBloomScans << ','
            << scan.cumulativeBackwardBloomScans << ",0,0," << (scan.isTypeSplit ? 1 : 0)
            << ',' << scan.typeIndex << ',' << scan.typeCount << ",na\n";
    }
}

template <class Search>
void WriteIDTHSScans(std::ofstream &out, const Options &options, const Search &search)
{
    for (const auto &scan : search.GetIterationStats()) {
        out << options.domain << ',' << options.instance << ",idths_trans,"
            << options.heuristic << ',' << options.f2f << ',' << options.consistency << ','
            << scan.boundCycle << ',' << scan.totalBound << ',' << scan.forwardBound << ','
            << scan.backwardBound << ",0,both,"
            << (scan.forwardExpanded + scan.backwardExpanded) << ',' << scan.cumulativeExpanded
            << ',' << scan.forwardCandidates << ",na,na,na,na,na,na,na,na,"
            << scan.forwardScans << ',' << scan.backwardScans << ','
            << scan.tableFullBackwardScans << ',' << scan.endCycleBackwardScans
            << ",0,na,na," << (scan.solved ? 1 : 0) << '\n';
    }
}

void WriteSummary(const Options &options, const RunResult &result, size_t stateBits,
                  double sizeKiB, uint64_t benchmarkSeed)
{
    std::ofstream out(options.summaryOutput);
    if (!out) throw std::runtime_error("Unable to open summary output: " + options.summaryOutput);
    out << "domain,instance,algorithm,heuristic,f2f,consistency,status,error,setup_seconds,"
           "search_seconds,time_limit_seconds,solution_length,memory_states,memory_items,frontier_items,logical_state_bits,bloom_size_kib,"
           "k_hashes,bloom_seed,benchmark_seed,nodes_expanded,nodes_generated,"
           "forward_nodes_expanded,forward_nodes_generated,backward_nodes_expanded,"
           "backward_nodes_generated,bound_cycles,"
           "forward_scans,backward_scans,extraction_scans,table_full_scans,end_cycle_scans,"
           "peak_inserted,peak_estimated_unique,peak_bits_set,peak_fill_ratio,peak_estimated_fp,"
           "peak_materialized,actual_f2f_pruned,actual_consistency_pruned,"
           "shadow_f2f_would_prune,shadow_consistency_would_prune,shadow_both_would_prune,"
           "counterfactual_scope,memory_accounting,memory_budget_bytes,exact_state_capacity,idths_peak_stored,strict_storage_limit,bloom_size_bytes,pancake_size,pancake_set,rubik_walk_length,rubik_walk_seed,bihs_peak_exact_stored,aem_enabled,aem_alpha,aem_trigger_mode,aem_attempts,aem_overflows,aem_peak_managed_bytes,aem_bloom_buffer_bytes\n";
    out << options.domain << ',' << options.instance << ',' << options.algorithm << ','
        << options.heuristic << ',' << options.f2f << ',' << options.consistency << ','
        << result.status << ',' << EscapeCSV(result.error) << ',' << std::setprecision(12)
        << result.setupSeconds << ',' << result.searchSeconds << ','
        << (IsBiHS(options.algorithm) ? options.bihsTimeoutSeconds
                                             : options.idthsTimeoutSeconds)
        << ',' << result.solutionLength << ','
        << options.memoryStates << ',' << result.memoryItems << ',' << result.frontierItems
        << ',' << stateBits << ','
        << (IsAEM(options.algorithm) ? result.actualBloomBytes / 1024.0 : sizeKiB)
        << ',' << options.kHashes
        << ',' << options.bloomSeed << ',' << benchmarkSeed << ',' << result.expanded << ','
        << result.generated << ',' << result.forwardExpanded << ',' << result.forwardGenerated
        << ',' << result.backwardExpanded << ',' << result.backwardGenerated << ','
        << result.boundCycles << ',' << result.forwardScans << ','
        << result.backwardScans << ',' << result.extractionScans << ',' << result.tableFullScans
        << ',' << result.endCycleScans << ',' << result.peakInserted << ','
        << result.peakEstimatedUnique << ',' << result.peakBitsSet << ',' << result.peakFillRatio
        << ',' << result.peakEstimatedFP << ',' << result.peakMaterialized << ',';
    if (options.f2f == "na") out << "na";
    else out << result.actualF2FPruned;
    out << ',' << result.actualConsistencyPruned << ',';
    if (options.f2f == "na") out << "na";
    else out << result.shadowF2FWouldPrune;
    out << ',' << result.shadowConsistencyWouldPrune << ',';
    if (options.f2f == "na") out << "na";
    else out << result.shadowBothWouldPrune;
    out << ",local_test_not_global_savings,"
        << (IsAEM(options.algorithm) ? "managed_bloom_and_exact_buffers_excludes_pdb_traversal_stats_allocator" : "logical_frontier_budget_excludes_auxiliary_structures") << ',' 
        << (options.memoryBytes ? options.memoryBytes : static_cast<uint64_t>(sizeKiB * 1024))
        << ',' << options.memoryStates << ',' << result.idthsPeakStored
        << ',' << (options.memoryBytes ? 1 : 0) << ','
        << (IsAEM(options.algorithm) ? result.actualBloomBytes : (IsBiHS(options.algorithm) ? static_cast<uint64_t>(sizeKiB * 1024) : 0))
        << ',' << options.pancakeSize << ',' << options.pancakeSet
        << ',' << options.rubikWalkLength << ',' << options.rubikWalkSeed
        << ',' << result.bihsPeakExactStored << ',' << IsAEM(options.algorithm) << ','
        << (IsAEM(options.algorithm) ? options.aemAlpha : 0) << ','
        << (options.algorithm == "bihs_bloom_aem_control" ? "insertion_count" : (IsAEM(options.algorithm) ? "occupancy_or_count" : "na"))
        << ',' << result.aemAttempts << ',' << result.aemOverflows << ',' << result.aemPeakManaged
        << ',' << result.actualBloomBytes << '\n';
    if (!options.bucketOutput.empty()) {
        std::ofstream buckets(options.bucketOutput);
        if (!buckets) throw std::runtime_error("Unable to open bucket output");
        buckets << "domain,instance,algorithm,heuristic,status,memory_states,final_iteration_buckets,scope\n"
                << options.domain << ',' << options.instance << ',' << options.algorithm << ','
                << options.heuristic << ',' << result.status << ',' << options.memoryStates << ','
                << ((result.status == "ok" || result.status == "timeout")
                        ? result.finalBuckets : "na")
                << ",final_bound_iteration_only\n";
    }
}

template <class State, class Action, class Environment>
RunResult RunDomain(Options options, State start, State goal, Environment &environment,
                    double setupSeconds, size_t stateBits, uint64_t benchmarkSeed)
{
    RunResult result;
    result.setupSeconds = setupSeconds;
    if (options.memoryBytes) {
        if (options.memoryBytes > std::numeric_limits<uint64_t>::max() / 8)
            throw std::runtime_error("Byte budget overflows bit count");
        options.memoryStates = options.memoryBytes * 8 / stateBits;
        if (options.memoryStates == 0)
            throw std::runtime_error("Byte budget cannot hold one logical state");
    }
    if (options.memoryStates > std::numeric_limits<uint64_t>::max() / stateBits)
        throw std::runtime_error("State budget overflows bit count");
    const uint64_t logicalBits = options.memoryStates * static_cast<uint64_t>(stateBits);
    const bool fixedMemory = IsBiHS(options.algorithm) ||
                             options.algorithm == "idths_trans";
    const double sizeKiB = fixedMemory
        ? (options.memoryBytes ? options.memoryBytes / 1024.0
                               : std::max<uint64_t>(1, (logicalBits + 8191) / 8192))
        : 0;
    std::cout << "memory_budget_bytes=" << options.memoryBytes
              << " exact_state_capacity=" << options.memoryStates << '\n';

    std::ofstream levels(options.levelOutput);
    std::ofstream scans(options.scanOutput);
    if (!levels) throw std::runtime_error("Unable to open level output: " + options.levelOutput);
    if (!scans) throw std::runtime_error("Unable to open scan output: " + options.scanOutput);
    WriteLevelHeader(levels);
    WriteScanHeader(scans);

    if (!options.recordStatus.empty()) {
        result.status = options.recordStatus;
        result.error = options.recordError;
        WriteSummary(options, result, stateBits, sizeKiB, benchmarkSeed);
        return result;
    }

    try {
        const auto begin = std::chrono::steady_clock::now();
        if (options.algorithm == "astar" || options.algorithm == "rev_astar") {
            TemplateAStar<State, Action, Environment> search;
            std::vector<State> path;
            if (options.algorithm == "astar")
                search.GetPath(&environment, start, goal, path);
            else
                search.GetPath(&environment, goal, start, path);
            result.searchSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - begin).count();
            result.status = path.empty() ? "failed" : "ok";
            result.solutionLength = path.empty() ? -1 : static_cast<int>(path.size()) - 1;
            result.expanded = search.GetNodesExpanded();
            result.memoryItems = search.GetNumItems();
        } else if (options.algorithm == "mm") {
            MM<State, Action, Environment> search;
            std::vector<State> path;
            Environment forwardEnvironment = environment;
            Environment backwardEnvironment = environment;
            search.GetPath(&environment, start, goal,
                           &forwardEnvironment, &backwardEnvironment, path);
            result.searchSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - begin).count();
            result.status = path.empty() ? "failed" : "ok";
            result.solutionLength = path.empty() ? -1 : static_cast<int>(path.size()) - 1;
            result.expanded = search.GetNodesExpanded();
            result.memoryItems = search.GetNumForwardItems() + search.GetNumBackwardItems();
            result.frontierItems = search.GetNumForwardItems();
        } else if (IsBiHS(options.algorithm)) {
            using Search = BiHSBloom<State, Action, Environment>;
            Search search(options.memoryBytes ? 1 : static_cast<int>(sizeKiB), options.kHashes, environment);
            if (options.memoryBytes) search.SetMemoryBudgetBytes(options.memoryBytes);
            search.SetBloomSeed(options.bloomSeed);
            search.SetUseConsistentErrorBound(options.consistency != 0);
            search.SetTimeLimitSeconds(options.bihsTimeoutSeconds);
            if (IsAEM(options.algorithm)) {
                search.EnableAEM(options.aemAlpha, options.algorithm == "bihs_bloom_aem_control",
                                 options.aemExactBatches);
                result.actualBloomBytes = search.GetAEMBloomBytes();
            }
            if (options.streamingNoLimit) search.EnableStreamingWithoutLoadLimit();
            else if (options.streamingQStop > 0) search.EnableStreaming(options.streamingQStop);
            std::vector<Action> path;
            try {
                path = search.GetPath(start, goal);
            } catch (const typename Search::DeadlineExceeded&) {
                // The search object retains partial counters for the timeout row.
            }
            result.searchSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - begin).count();
            result.bihsPeakExactStored = search.GetPeakExactStoredStates();
            if (IsAEM(options.algorithm)) {
                search.WriteAEMEvents(options.aemOutput);
                result.aemAttempts = search.GetAEMAttempts();
                result.aemOverflows = search.GetAEMOverflows();
                result.aemPeakManaged = search.GetAEMPeakManagedBytes();
                if (search.GetAEMLiveManagedBytes()) throw std::logic_error("AEM storage leaked after search");
            }
            result.status = search.DidTimeOut() ? "timeout" :
                            (path.empty() ? "failed" : "ok");
            result.solutionLength = path.empty() ? -1 : static_cast<int>(path.size());
            if (!path.empty()) {
                State check = start;
                for (const auto &move : path) environment.ApplyAction(check, move);
                if (!(check == goal)) throw std::runtime_error("Returned BiHS path fails replay");
            }
            result.expanded = search.GetTotalNodesExpanded();
            result.generated = search.GetTotalNodesGenerated();
            result.finalBuckets = std::to_string(search.GetFinalIterationBuckets());
            const auto &scanStats = search.GetScanStats();
            std::cout << "streaming_q_stop=" << options.streamingQStop
                      << " stream_batches=" << scanStats.streamBatches << '\n';
            result.boundCycles = scanStats.boundCycles;
            result.forwardScans = scanStats.forwardBloomScans;
            result.backwardScans = scanStats.backwardBloomScans;
            result.extractionScans = scanStats.ExtractionScans();
            result.tableFullScans = search.GetAEMBatchFlushes();
            WriteBiHSLevels(levels, options, search, result);
            WriteBiHSScans(scans, options, search, result);
        } else {
            const bool f2fApplicable = options.domain != "toh";
            const bool f2f = f2fApplicable && options.f2f == "1";
            using Search = IDTHSwTrans<State, Action, false>;
#ifdef PAPER_BUCKET_BASELINE
            Search search(f2f, options.consistency != 0, true, 1, true);
            search.SetStrictStorageLimit(options.memoryBytes != 0);
            const bool solved = search.GetPath(&environment, start, goal,
                                               options.idthsTimeoutSeconds,
                                               static_cast<unsigned long>(options.memoryStates));
#else
            Search search(f2f, options.consistency != 0, true, 1, false);
            search.SetStrictStorageLimit(options.memoryBytes != 0);
            search.SetCollectF2FShadow(f2fApplicable);
            const bool solved = search.GetPath(&environment, start, goal,
                                               static_cast<unsigned long>(options.memoryStates));
#endif
            result.searchSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - begin).count();
#ifdef PAPER_BUCKET_BASELINE
            result.status = solved ? "ok" : (search.DidTimeOut() ? "timeout" : "failed");
#else
            result.status = solved ? "ok" : "failed";
#endif
            result.solutionLength = solved ? static_cast<int>(search.getPathLength()) : -1;
            result.idthsPeakStored = search.GetPeakStoredStates();
            result.expanded = search.GetNodesExpanded();
            result.finalBuckets = std::to_string(search.GetFinalIterationBuckets());
#ifdef PAPER_BUCKET_BASELINE
            result.generated = search.GetNodesTouched();
            result.forwardExpanded = search.GetForwardNodesExpanded();
            result.forwardGenerated = search.GetForwardNodesGenerated();
            result.backwardExpanded = search.GetBackwardNodesExpanded();
            result.backwardGenerated = search.GetBackwardNodesGenerated();
            result.backwardScans = search.GetBackwardScans();
#else
            result.generated = search.GetNodesGenerated();
            const auto &scanStats = search.GetScanStats();
            result.boundCycles = scanStats.boundCycles;
            result.forwardScans = scanStats.forwardScans;
            result.backwardScans = scanStats.backwardScans;
            result.tableFullScans = scanStats.backwardTableFullScans;
            result.endCycleScans = scanStats.backwardEndOfCycleScans;
            WriteIDTHSLevels(levels, options, search, f2fApplicable, result);
            WriteIDTHSScans(scans, options, search);
#endif
        }
    }
    catch (const std::bad_alloc&) {
        result.status = "oom";
        result.error = "std::bad_alloc";
    }
    catch (const std::exception &error) {
        result.status = "error";
        result.error = error.what();
    }

    WriteSummary(options, result, stateBits, sizeKiB, benchmarkSeed);
    std::cout << "domain=" << options.domain << " instance=" << options.instance
              << " algorithm=" << options.algorithm << " heuristic=" << options.heuristic
              << " f2f=" << options.f2f << " consistency=" << options.consistency
              << " status=" << result.status << " search_seconds=" << result.searchSeconds
              << " expanded=" << result.expanded << " generated=" << result.generated
              << " solution_length=" << result.solutionLength << '\n' << std::flush;
    return result;
}

RunResult RunSTP(const Options &options)
{
    STPEnvironment environment;
    STPState goal;
    goal.Reset();
    STPState start = STP::GetKorfInstance(options.instance);
    return RunDomain<STPState, slideDir>(options, start, goal, environment, 0.0,
                                        get_state_size(start), options.instance);
}

template<int N> RunResult RunPancakeSize(const Options &options)
{
    const int gap = options.heuristic == "gap" ? 0 :
                    (options.heuristic == "gap-1" ? 1 : options.heuristic == "gap-2" ? 2 : 3);
    PancakePuzzle<N> environment(gap);
    PancakePuzzleState<N> goal, start;
    if (options.pancakeSet == "random") {
        GlibcRandom random(options.pancakeSeed);
        for (int instance = 0; instance <= options.instance; ++instance) {
            start = goal;
            for (int index = 0; index < N; ++index) {
                int other = index + random.Next() % (N - index);
                std::swap(start.puzzle[index], start.puzzle[other]);
            }
        }
    } else if (options.pancakeSet == "composition") {
        if (!::GetPancakeInstance(start, options.instance))
            throw std::runtime_error("Missing published composition instance");
    } else {
        for (int i = 0; i < N; ++i) start.puzzle[i] = i ^ 1;
    }
    std::cout << "pancake_size=" << N << " pancake_set=" << options.pancakeSet << " endpoint=";
    for (int i = 0; i < N; ++i) std::cout << (i ? ":" : "") << start.puzzle[i];
    std::cout << '\n';
    return RunDomain<PancakePuzzleState<N>, PancakePuzzleAction>(
        options, start, goal, environment, 0.0, get_state_size(start), options.pancakeSeed);
}

RunResult RunPancake(const Options &options) {
    if (options.pancakeSize == 14) return RunPancakeSize<14>(options);
    if (options.pancakeSize == 16) return RunPancakeSize<16>(options);
    if (options.pancakeSize == 20) return RunPancakeSize<20>(options);
    return RunPancakeSize<12>(options);
}

template <int Large, int Small>
RunResult RunTOHPartition(const Options &options)
{
    using State = TOHState<kTOHDisks>;
    using Environment = TOHPDBEnvironment<kTOHDisks, Large, Small>;
    const auto endpoints = GetTOHInstance<kTOHDisks>(options.instance);
    const auto setupBegin = std::chrono::steady_clock::now();
    Environment environment(endpoints.first, endpoints.second);
    const double setupSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - setupBegin).count();
    return RunDomain<State, TOHMove>(options, endpoints.first, endpoints.second, environment,
                                     setupSeconds, get_state_size(State{}),
                                     GetTOHSeed(options.instance));
}

RunResult RunTOH(const Options &options)
{
    if (options.heuristic == "pdb10+4") return RunTOHPartition<10, 4>(options);
    return RunTOHPartition<12, 2>(options);
}

RunResult RunRubik(const Options &options) {
    const auto begin = std::chrono::steady_clock::now();
    StrongRubik environment(options.rubikPDBDir);
    RubiksState start, goal;
    if (options.rubikWalkLength >= 0) {
        GlibcRandom random(options.rubikWalkSeed);
        int lastFace = -1;
        std::cout << "instance_protocol=random_walk moves=";
        for (int i = 0; i < options.rubikWalkLength; ++i) {
            int a;
            do { a = random.Next() % 18; } while (a / 3 == lastFace);
            environment.ApplyAction(start, a); lastFace = a / 3;
            std::cout << (i ? ":" : "") << a;
        }
        std::cout << " endpoint_hash=" << environment.GetStateHash(start) << '\n';
    } else if (options.rubikSmokeDepth >= 0) {
        GlibcRandom random(20260913U + options.instance);
        int lastFace = -1;
        for (int i=0; i<options.rubikSmokeDepth; ++i) {
            int a;
            do { a = random.Next()%18; } while (a/3 == lastFace);
            environment.ApplyAction(start,a); lastFace=a/3;
        }
        std::cout << "instance_protocol=smoke_scramble depth=" << options.rubikSmokeDepth << '\n';
    } else {
        RubiksCubeInstances::GetKorfRubikInstance(start, options.instance);
        std::cout << "instance_protocol=korf100\n";
    }
    std::cout << "pdb_table_bytes=" << environment.PDBBytes() << " initial_h=" << environment.HCost(start,goal) << '\n';
    return RunDomain<RubiksState,RubiksAction>(options,start,goal,environment,
        std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count(),
        get_state_size(start), options.rubikSmokeDepth >= 0 ? 20260913U+options.instance : options.instance);
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc == 3 && std::string(argv[1]) == "--aem-layout") {
            const auto bytes = ParseUInt64(argv[2], "--aem-layout");
            BiHSBloom<TOHState<14>, TOHMove, TOH<14>> search(1, 1);
            search.SetMemoryBudgetBytes(bytes);
            search.EnableAEM();
            std::cout << "{\"managed_budget_bytes\":" << bytes
                      << ",\"bloom_buffer_bytes\":" << search.GetAEMBloomBytes()
                      << ",\"state_bits\":28}" << '\n';
            return 0;
        }
        const Options options = ParseOptions(argc, argv);
        ValidateOptions(options);
        RunResult result;
        if (options.domain == "stp") result = RunSTP(options);
        else if (options.domain == "pancake") result = RunPancake(options);
        else if (options.domain == "rubik") result = RunRubik(options);
        else result = RunTOH(options);
        return (result.status == "ok" || result.status == "timeout")
            ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception &error) {
        std::cerr << "comparison driver error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
