#include "MNPuzzle.h"
#include "PancakePuzzle.h"
#include "TOH.h"
#include <cstdint>
#include <stdexcept>
#include <string>

struct TestState { uint32_t value = 0; };
bool operator==(const TestState &a, const TestState &b) { return a.value == b.value; }
bool operator!=(const TestState &a, const TestState &b) { return !(a == b); }
size_t get_state_size(const TestState&) { return 5; }
template<int N> size_t get_state_size(const TOHState<N>&) { return 2 * N; }
#include "AEMTOHCodec.h"
#include "BiHSBloomAEM.h"

// A cyclic undirected graph; exact distances to the selected endpoint make
// path validity and bound progression independently checkable.
struct TestEnvironment {
    void GetActions(const TestState &s, std::vector<int> &out) const {
        out.clear();
        static const int edges[][2] = {{0,1},{0,2},{1,2},{1,6},{2,6},{1,3},{2,3},{3,4},{4,5}};
        for (const auto &e : edges) {
            if (s.value == uint32_t(e[0])) out.push_back(e[0] * 8 + e[1]);
            if (s.value == uint32_t(e[1])) out.push_back(e[1] * 8 + e[0]);
        }
    }
    void ApplyAction(TestState &s, int a) const {
        if (s.value != uint32_t(a / 8)) throw std::logic_error("Invalid test path");
        s.value = a % 8;
    }
    bool InvertAction(int &a) const { a = (a % 8) * 8 + a / 8; return true; }
    void UndoAction(TestState &s, int a) const { InvertAction(a); ApplyAction(s, a); }
    double HCost(const TestState&, const TestState&) const { return 0; }
};

void Require(bool yes, const char *message) { if (!yes) throw std::runtime_error(message); }

int main() {
    try {
        TOH<6> toh;
        TOHState<6> tohState;
        for (uint64_t rank = 0; rank < 4096; ++rank) {
            toh.GetStateFromHash(rank, tohState);
            Require(AEMStateCodec<TOHState<6>>::Encode(tohState) == rank, "TOH lossless codec disagrees with domain rank");
            Require(AEMStateCodec<TOHState<6>>::Equal(rank, tohState), "TOH exact codec equality failed");
        }
        using Table = AEMTable<TestState, int>;
        const size_t bytes = Table::BytesForCapacity(2, 2);
        Require(Table::CapacityForBytes(bytes, 2) == 2, "Wrong exact capacity accounting");
        Require(Table::CapacityForBytes(bytes - 1, 2) == 1, "Capacity rounds above budget");
        Table table(2, 2);
        Require(table.InsertIfAbsent(7, {1}, {1,2}), "First insertion failed");
        Require(!table.InsertIfAbsent(7, {1}, {3,4}), "Duplicate counted as new state");
        Require(table.InsertIfAbsent(7, {2}, {5,6}), "Hash collision lost a distinct state");
        Require(!table.InsertIfAbsent(7, {2}, {7,8}), "Full table rejects a duplicate");
        Require(table.Find(7, {3}) == nullptr, "Hash collision became exact equality");
        bool overflow = false;
        try { table.InsertIfAbsent(7, {3}, {1,2}); } catch (const AEMTableFull&) { overflow = true; }
        Require(overflow && table.size() == 2 && table.bytes() == bytes, "Overflow changed bounded storage");
        auto match = table.Find(7, {2});
        Require(match && match->second.size() == 2 && *match->second.begin() == 5,
                "Bounded path arena corrupted");
        Table rootTable(1, 0);
        rootTable.InsertIfAbsent(0, {0}, {});
        Require(rootTable.Find(0, {0})->second.size() == 0, "Zero-depth path failed");

        using Search = BiHSBloom<TestState, int, TestEnvironment>;
        TestEnvironment env;
        // Forced materialization overflow must leave the caller's endpoints
        // untouched even when recursive traversal throws before backtracking.
        Search materializer(1, 1, env);
        materializer.SetMemoryBudgetBytes(4096);
        materializer.SetTimeLimitSeconds(30);
        BiHSBloomFilter<TestState> saturated(1, 1, 42);
        saturated.add_hash(0);
        Table tiny(1, 2);
        TestState origin{0}, goal{5};
        overflow = false;
        try { materializer.FillAEMTable(origin, goal, 2, 100, &saturated, tiny); }
        catch (const AEMTableFull&) { overflow = true; }
        Require(overflow && origin.value == 0 && goal.value == 5, "Overflow mutated endpoints");

        // A deliberately saturated supplied filter underestimates cardinality.
        // TryAEM must turn a bounded overflow into unresolved fallback.
        Search fallback(1, 1, env);
        fallback.SetMemoryBudgetBytes(640);
        fallback.EnableAEM(0.5);
        std::vector<std::unique_ptr<BiHSBloomFilter<TestState>>> liveFilters;
        for (int i = 0; i < 3; ++i) {
            BiHSBloomFilter<TestState> *raw = nullptr;
            fallback.InitBloom(raw);
            liveFilters.emplace_back(raw);
        }
        double failed = INFINITY;
        auto result = fallback.TryAEM(origin, goal, 2, 2, &saturated, true, failed);
        Require(!result.resolved && fallback.GetAEMOverflows() == 1,
                "Overflow did not return to unresolved Bloom fallback");
        liveFilters.clear();
        Require(fallback.GetAEMLiveManagedBytes() == 0, "Exact storage leaked after attempt");
        Require(fallback.GetAEMPeakManagedBytes() <= 640, "Attempt exceeded managed memory");

        Search batched(1, 1, env);
        batched.SetMemoryBudgetBytes(640);
        batched.EnableAEM(1.0, false, true);
        for (int i = 0; i < 3; ++i) {
            BiHSBloomFilter<TestState> *raw = nullptr;
            batched.InitBloom(raw);
            liveFilters.emplace_back(raw);
        }
        failed = INFINITY;
        auto batchResult = batched.TryAEM(origin, goal, 2, 2, &saturated, true, failed);
        Require(batchResult.resolved && batchResult.path.size() == 4 &&
                batched.GetAEMBatchFlushes() > 0 && batched.GetScanStats().intersectionScans > 1,
                "A miss in a full batch must continue to a match in a later batch");
        TestState replay = origin;
        for (int move : batchResult.path) env.ApplyAction(replay, move);
        Require(replay == goal, "Batched extraction returned an invalid path");
        liveFilters.clear();
        Require(batched.GetAEMLiveManagedBytes() == 0 && batched.GetAEMPeakManagedBytes() <= 640,
                "Batched extraction exceeded or leaked managed memory");

        Search halfBloom(1, 1, env);
        constexpr size_t halfBloomTestBudget = 4097;
        halfBloom.SetMemoryBudgetBytes(halfBloomTestBudget);
        halfBloom.SetBloomSeed(42);
        halfBloom.EnableAEM(1.0, false, true, true);
        auto typedSeed = halfBloom.RebuildTypedSeed(origin, goal, 1, 1, 0);
        Require(halfBloom.GetAEMLiveManagedBytes() == halfBloomTestBudget / 2,
                "One managed filter must occupy half the budget, rounded down");
        const auto before = halfBloom.GetAEMSeedRebuilds();
        const auto typedPath = halfBloom.SolveAtDepthByTypesFromSeed(
            origin, goal, 1, 1, std::move(typedSeed), 2, 0);
        Require(typedPath.empty() && halfBloom.GetAEMSeedRebuilds() == before + 1,
                "A typed miss must rebuild the seed only for the next partition");
        Require(halfBloom.GetAEMLiveManagedBytes() == 0 &&
                halfBloom.GetAEMPeakManagedBytes() <= halfBloomTestBudget,
                "Half-budget typed search exceeded or leaked managed memory");

        MNPuzzle<4, 4> stp;
        MNPuzzleState<4, 4> tileState;
        using TileFingerprint = BiHSBloomHelper::StateFingerprint<MNPuzzleState<4, 4>, slideDir>;
        std::vector<slideDir> tileMoves;
        for (size_t step = 0; step < 64; ++step) {
            stp.GetActions(tileState, tileMoves);
            const auto move = tileMoves[step % tileMoves.size()];
            const auto nextHash = TileFingerprint::apply(TileFingerprint::hash(tileState), tileState, move);
            stp.ApplyAction(tileState, move);
            Require(nextHash == TileFingerprint::hash(tileState), "Incremental STP fingerprint mismatch");
        }

        for (bool control : {false, true}) {
            for (double alpha : {0.25, 0.5, 0.75}) {
                uint64_t previous = 0;
                for (int repeat = 0; repeat < 2; ++repeat) {
                    Search search(1, 2, env);
                    search.SetMemoryBudgetBytes(4096);
                    search.SetBloomSeed(42);
                    search.SetTimeLimitSeconds(10);
                    search.EnableAEM(alpha, control);
                    auto path = search.GetPath(origin, goal);
                    Require(path.size() == 4, "AEM differs from independently known graph distance");
                    TestState replay = origin;
                    for (int move : path) env.ApplyAction(replay, move);
                    Require(replay == goal, "AEM returned an invalid path");
                    Require(search.GetAEMAttempts() > 0, "AEM never attempted exact materialization");
                    Require(search.GetAEMPeakManagedBytes() <= 4096, "Search exceeded managed budget");
                    Require(search.GetAEMLiveManagedBytes() == 0, "Search leaked live frontier storage");
                    if (repeat) Require(previous == search.GetTotalNodesExpanded(), "Nondeterministic AEM output");
                    previous = search.GetTotalNodesExpanded();
                }
            }
        }
        for (double alpha : {0.0, 1.0, -1.0, double(NAN), double(INFINITY)}) {
            bool rejected = false;
            try { Search search(1, 1, env); search.EnableAEM(alpha); }
            catch (const std::invalid_argument&) { rejected = true; }
            Require(rejected, "Invalid AEM alpha accepted");
        }
        BiHSBloomFilter<TestState> filter(8192, 3, 42);
        for (uint64_t i = 0; i < 100; ++i) filter.add_hash(i);
        for (uint64_t i = 0; i < 100; ++i) Require(filter.maybe_contains_hash(i), "Bloom false negative");
        std::cout << "PASS: bounded exact collisions/deduplication/overflow, endpoint restoration, "
                     "optimal replay, alpha/control modes, deterministic output, managed memory, Bloom membership\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
