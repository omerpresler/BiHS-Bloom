#include "Common.h"
#include "MNPuzzle.h"
#include "TemplateAStar.h"
#include "Timer.h"
#include "Driver.h"

#include <iostream>
#include <vector>
#include <queue>
#include <unordered_set>
#include <random>
#include <stdexcept>
#include <fstream>
#include <string>
#include <algorithm> // for std::shuffle
#include <cstring>   // for strcmp

int solve(MNPuzzleState<MN_SIZE, MN_SIZE> start,
          MNPuzzleState<MN_SIZE, MN_SIZE> goal)
{
    MNPuzzle<MN_SIZE, MN_SIZE> mnp;
    TemplateAStar<MNPuzzleState<MN_SIZE, MN_SIZE>, slideDir,
                  MNPuzzle<MN_SIZE, MN_SIZE>> astar;
    std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> path;

    Timer t;
    t.StartTimer();
    astar.GetPath(&mnp, start, goal, path);
    t.EndTimer();

    std::cout << "Path found length: " << path.size() << std::endl;
    std::cout << "Nodes expanded: " << astar.GetNodesExpanded() << std::endl;
    std::cout << "Time: " << t.GetElapsedTime() << std::endl;

    return static_cast<int>(path.size());
}

// A function that gets a distance and returns 'amount' states whose
// optimal solution length from 'start' is exactly 'distance' moves.
std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>>
generateRandomState(int distance,
                    int amount,
                    const MNPuzzleState<MN_SIZE, MN_SIZE> &start)
{
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

    // Initialize BFS
    q.push({start, 0});
    visited.insert(mnp.GetStateHash(start));

    std::vector<slideDir> acts;

    // BFS up to depth = distance
    while (!q.empty()) {
        Node cur = q.front();
        q.pop();

        // If we reached the desired depth, record the state and do not expand it further
        if (cur.depth == distance) {
            layerStates.push_back(cur.state);
            continue;
        }

        if (cur.depth > distance) {
            // Should not happen, but guard just in case
            continue;
        }

        acts.clear();
        mnp.GetActions(cur.state, acts);
        for (slideDir a : acts) {
            MNPuzzleState<MN_SIZE, MN_SIZE> next = cur.state;
            mnp.ApplyAction(next, a);
            uint64_t h = mnp.GetStateHash(next);

            // Standard BFS "visited" check to enforce uniqueness
            if (visited.insert(h).second) {
                q.push({next, cur.depth + 1});
            }
        }
    }

    // We now have all states at exact distance 'distance' in layerStates
    if (static_cast<int>(layerStates.size()) < amount) {
        throw std::runtime_error(
            "Not enough states at the requested distance; "
            "reduce 'amount' or 'distance'.");
    }

    // Randomly pick 'amount' unique states from this layer
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(layerStates.begin(), layerStates.end(), gen);

    layerStates.resize(amount);
    return layerStates;
}

void printUsage(const char* progName) {
    std::cerr << "Usage:\n"
              << "  Generate: " << progName << " --generate -d <dist> -n <amount> -f <file>\n"
              << "  Solve:    " << progName << " --solve -f <file>\n";
}

int main(int argc, char** argv)
{
    bool generate = false;
    bool solveMode = false;
    int distance = -1;
    int amount = -1;
    std::string filename;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--generate") == 0 || strcmp(argv[i], "-g") == 0) {
            generate = true;
        } else if (strcmp(argv[i], "--solve") == 0 || strcmp(argv[i], "-s") == 0) {
            solveMode = true;
        } else if (strcmp(argv[i], "--distance") == 0 || strcmp(argv[i], "-d") == 0) {
            if (i + 1 < argc) distance = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--amount") == 0 || strcmp(argv[i], "-n") == 0) {
            if (i + 1 < argc) amount = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) {
            if (i + 1 < argc) filename = argv[++i];
        }
    }

    if ((generate && solveMode) || (!generate && !solveMode)) {
        std::cerr << "Error: Must specify exactly one of --generate or --solve.\n";
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
            std::cerr << "Error: For generation, distance must be >= 0 and amount > 0.\n";
            return 1;
        }

        MNPuzzleState<MN_SIZE, MN_SIZE> start;
        start.Reset(); // assumes Reset() sets solved configuration

        std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> states;
        try {
            states = generateRandomState(distance, amount, start);
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
                  << " puzzle states at distance " << distance
                  << " and saved to " << filename << "\n";
    } else { // solveMode
        std::vector<MNPuzzleState<MN_SIZE, MN_SIZE>> puzzles;
        // read_in_mn_puzzles(filename, first_counter, max_puzzles, vector)
        // Assuming first_counter=false (just raw states), max_puzzles=10000 (arbitrary large)
        MNPuzzle<MN_SIZE, MN_SIZE>::read_in_mn_puzzles(filename.c_str(), false, 10000, puzzles);

        if (puzzles.empty()) {
            std::cerr << "No puzzles loaded from " << filename << "\n";
            return 1;
        }

        MNPuzzleState<MN_SIZE, MN_SIZE> goal;
        goal.Reset();

        std::cout << "Loaded " << puzzles.size() << " puzzles. Solving...\n";
        for (size_t i = 0; i < puzzles.size(); ++i) {
            std::cout << "Puzzle " << i << ": ";
            solve(puzzles[i], goal);
        }
    }

    return 0;
}
