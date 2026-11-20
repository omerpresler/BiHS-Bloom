/*
 *  IDTHSwTrans.h
 *
 *  Created by Steven Danishevski on MARCH 20.
 *
 */

#ifndef IDTHS_TRANS_H
#define IDTHS_TRANS_H

#include <deque>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <ext/hash_map>
#include "AStarOpenClosed.h"
#include "SearchEnvironment.h"
#include <math.h>

template <typename state>
class transpostionNode
{
public:
	transpostionNode(const state &theData, double gCost, double hCost, double error)
		: data(theData), g(gCost), h(hCost), error(error) {}
	state data;
	double g;
	double h;
	double error;
};

template <class state, class action, bool verbose = true, class table = std::vector<transpostionNode<state>>>
class IDTHSwTrans
{
public:
	struct ScanStats
	{
		uint64_t boundCycles = 0;
		uint64_t forwardScans = 0;
		uint64_t backwardScans = 0;
		uint64_t backwardTableFullScans = 0;
		uint64_t backwardEndOfCycleScans = 0;
		uint64_t onlyForwardEmptyCycles = 0;
	};

	struct IterationStat
	{
		uint64_t boundCycle = 0;
		double totalBound = 0;
		double forwardBound = 0;
		double backwardBound = 0;
		uint64_t forwardExpanded = 0;
		uint64_t backwardExpanded = 0;
		uint64_t cumulativeExpanded = 0;
		size_t forwardCandidates = 0;
		bool backwardRan = false;
		bool onlyForwardEmpty = false;
		bool solved = false;
		uint64_t forwardScans = 0;
		uint64_t backwardScans = 0;
		uint64_t tableFullBackwardScans = 0;
		uint64_t endCycleBackwardScans = 0;
	};

	// Fine-grained work and pruning counters. A row identifies one ID threshold
	// cycle, one concrete forward/backward scan, and one unit-cost g-depth.
	// Counterfactual fields are local node/candidate tests only; they do not
	// predict the complete search tree of a run with different flags.
	struct LevelStat
	{
		uint64_t boundCycle = 0;
		uint64_t scanOrdinal = 0;
		bool isForward = true;
		double totalBound = 0;
		double directionBound = 0;
		int depth = 0;
		uint64_t visits = 0;
		uint64_t expanded = 0;
		uint64_t generated = 0;
		uint64_t accepted = 0;
		uint64_t frontierInserted = 0;
		uint64_t basePruned = 0;
		uint64_t duplicatePruned = 0;
		uint64_t candidateChecks = 0;
		uint64_t candidatesPruned = 0;
		uint64_t directionDepthPruned = 0;
		uint64_t actualF2FPruned = 0;
		uint64_t actualConsistencyPruned = 0;
		uint64_t actualFeatureOverlap = 0;
		uint64_t shadowF2FWouldPrune = 0;
		uint64_t shadowConsistencyWouldPrune = 0;
		uint64_t shadowBothWouldPrune = 0;
	};

	IDTHSwTrans(bool front2frontH = false, bool useConsistentErrorBound = false,
				  bool isUpdateByWorkload = false, double smallestEdge = 1,
				  bool useHash = true)
	{
		this->front2frontH = front2frontH;
		this->useConsistentErrorBound = useConsistentErrorBound;
		this->smallestEdge = smallestEdge;
		this->isUpdateByWorkload = isUpdateByWorkload;
		this->useHash = useHash;
	}
	virtual ~IDTHSwTrans() {}
	bool GetPath(SearchEnvironment<state, action> *env, state fromState, state toState,
				 unsigned long availableStorage = 0);
	double getPathLength() { return pathLength; }
	uint64_t GetNodesExpanded() { return nodesExpanded; }
	uint64_t GetNecessaryExpansions() { return necessaryExpansions; }
	uint64_t GetNodesTouched() { return nodesTouched; }
	uint64_t GetNodesGenerated() const { return nodesGenerated; }
	void ResetNodeCount() { nodesExpanded = nodesTouched = nodesGenerated = 0; }
	unsigned long getDMMExpansions() { return dMMExpansions; }
	const ScanStats &GetScanStats() const { return scanStats; }
	uint64_t GetFinalIterationBuckets() const {
		return iterationStats.empty() ? 0 : iterationStats.back().backwardScans;
	}
	const std::vector<IterationStat> &GetIterationStats() const { return iterationStats; }
	const std::deque<LevelStat> &GetLevelStats() const { return levelStats; }
	void SetCollectF2FShadow(bool enabled) { collectF2FShadow = enabled; }

private:
	unsigned long nodesExpanded, nodesTouched, dMMExpansions;
	uint64_t nodesGenerated = 0;
	double backwardBound;
	double forwardBound;
	double fBound;
	double pathLength = std::numeric_limits<double>::max();
	bool detectDuplicates;
	bool useHash;
	bool DoIterationForward(SearchEnvironment<state, action> *env,
							state parent, state currState, uint64_t hashValue, double g, uint64_t &midState);
	bool DoIterationBackward(SearchEnvironment<state, action> *env, state parent, state currState, double g, uint64_t &midState);
	double updateBoundByFraction(double boundToSplit, double p = 0.5, bool isInteger = true);
	double calculateBoundByWorkload(double newbound, double prevBound, double oldForwardBound, uint64_t forwardLoad, uint64_t backwardLoad);
	double nextBound;
	void UpdateNextBound(double fCost);
	state originGoal;
	state originStart;
	unsigned long dMMLastIterExpansions = 0;
	unsigned long necessaryExpansions = 0;
	unsigned long nodesExpandedSoFar = 0;
	unsigned long previousIterationExpansions = 0;
	uint64_t forwardExpandedInLastIter = 0;
	uint64_t backwardExpandedInLastIter = 0;

	unsigned long availableStorage = 0;

	typedef __gnu_cxx::hash_map<uint64_t, double, AHash64> IndexTable;
	IndexTable hashTable;
	table transTable;
	bool readyOpenLists = false;
	bool front2frontH;
	bool firstBounds;
	bool useConsistentErrorBound;
	bool collectF2FShadow = true;
	double smallestEdge;
	bool isUpdateByWorkload;
	ScanStats scanStats;
	std::vector<IterationStat> iterationStats;
	// Recursive scans keep a reference to the current depth's counters while
	// visiting children. deque preserves those references when deeper rows are
	// appended; vector growth would invalidate them and corrupt memory.
	std::deque<LevelStat> levelStats;
	std::vector<size_t> activeLevelIndices;
	uint64_t cachedLevelBoundCycle = std::numeric_limits<uint64_t>::max();
	uint64_t cachedLevelScanOrdinal = std::numeric_limits<uint64_t>::max();
	bool cachedLevelForward = true;
	LevelStat &GetLevelStat(bool isForward, double g);
};

template <class state, class action, bool verbose, class table>
typename IDTHSwTrans<state, action, verbose, table>::LevelStat &
IDTHSwTrans<state, action, verbose, table>::GetLevelStat(bool isForward, double g)
{
	const uint64_t scanOrdinal = isForward ? scanStats.forwardScans : scanStats.backwardScans;
	const int depth = static_cast<int>(std::llround(g));
	if (depth < 0) {
		throw std::runtime_error("IDTHSwTrans instrumentation received a negative depth");
	}
	if (cachedLevelBoundCycle != scanStats.boundCycles ||
		cachedLevelScanOrdinal != scanOrdinal || cachedLevelForward != isForward)
	{
		activeLevelIndices.clear();
		cachedLevelBoundCycle = scanStats.boundCycles;
		cachedLevelScanOrdinal = scanOrdinal;
		cachedLevelForward = isForward;
		const size_t missing = std::numeric_limits<size_t>::max();
		for (size_t index = 0; index < levelStats.size(); ++index)
		{
			const LevelStat &candidate = levelStats[index];
			if (candidate.boundCycle != scanStats.boundCycles ||
				candidate.scanOrdinal != scanOrdinal || candidate.isForward != isForward)
				continue;
			if (activeLevelIndices.size() <= static_cast<size_t>(candidate.depth))
				activeLevelIndices.resize(static_cast<size_t>(candidate.depth) + 1, missing);
			activeLevelIndices[candidate.depth] = index;
		}
	}
	const size_t missing = std::numeric_limits<size_t>::max();
	if (activeLevelIndices.size() <= static_cast<size_t>(depth))
		activeLevelIndices.resize(static_cast<size_t>(depth) + 1, missing);
	if (activeLevelIndices[depth] != missing)
		return levelStats[activeLevelIndices[depth]];

	levelStats.push_back(LevelStat{});
	activeLevelIndices[depth] = levelStats.size() - 1;
	LevelStat &result = levelStats.back();
	result.boundCycle = scanStats.boundCycles;
	result.scanOrdinal = scanOrdinal;
	result.isForward = isForward;
	result.totalBound = fBound;
	result.directionBound = isForward
		? forwardBound
		: fBound - forwardBound - smallestEdge;
	result.depth = depth;
	return result;
}

template <class state, class action, bool verbose, class table>
double IDTHSwTrans<state, action, verbose, table>::calculateBoundByWorkload(double newbound, double prevBound, double oldForwardBound, uint64_t forwardLoad, uint64_t backwardLoad)
{
	if (forwardLoad <= backwardLoad)
	{
		return oldForwardBound + newbound - prevBound;
	}
	else
	{
		return oldForwardBound;
	}
}

template <class state, class action, bool verbose, class table>
bool IDTHSwTrans<state, action, verbose, table>::GetPath(SearchEnvironment<state, action> *env,
													 state fromState, state toState, unsigned long availableStorage)
{
	this->availableStorage = availableStorage;
	nodesExpanded = nodesTouched = nodesGenerated = 0;
	scanStats = ScanStats{};
	iterationStats.clear();
	levelStats.clear();
	activeLevelIndices.clear();
	cachedLevelBoundCycle = std::numeric_limits<uint64_t>::max();
	cachedLevelScanOrdinal = std::numeric_limits<uint64_t>::max();
	originStart = fromState;
	originGoal = toState;
	double initialHeuristic = env->HCost(fromState, toState);
	fBound = nextBound = std::max(smallestEdge, initialHeuristic);
	forwardBound = fBound / 2 - smallestEdge;
	unsigned long nodesExpandedSoFar = 0;
	unsigned long previousIterationExpansions = 0;
	uint64_t midState;
	if (verbose)
	{
		printf("Starting solving with IDTHwTrans with %1.1llu memory\n", this->availableStorage);
	}
	while (true)
	{
		const uint64_t forwardScansBeforeCycle = scanStats.forwardScans;
		const uint64_t backwardScansBeforeCycle = scanStats.backwardScans;
		const uint64_t tableFullScansBeforeCycle = scanStats.backwardTableFullScans;
		const uint64_t endCycleScansBeforeCycle = scanStats.backwardEndOfCycleScans;
		scanStats.boundCycles++;
		dMMLastIterExpansions = 0;
		if (verbose)
		{
			printf("\tBounds: %1.1f(%1.1f, %1.1f): ", fBound, forwardBound, backwardBound);
		}
		uint64_t hash;
		if (useHash)
		{
			hash = env->GetStateHash(originStart);
		}
		scanStats.forwardScans++;
		const uint64_t backwardScansBefore = scanStats.backwardScans;
		const uint64_t forwardExpandedBefore = forwardExpandedInLastIter;
		const uint64_t backwardExpandedBefore = backwardExpandedInLastIter;
		bool solved = DoIterationForward(env, originStart, originStart, hash, 0, midState);
		const size_t forwardCandidates = transTable.size();
		if (verbose)
		{
			printf("Nodes expanded: %d(%d)\n", nodesExpanded - nodesExpandedSoFar, nodesExpanded);
		}

		bool backwardRan = false;
		bool onlyForwardEmpty = false;
		if (!solved && transTable.size() > 0)
		{
			scanStats.backwardScans++;
			scanStats.backwardEndOfCycleScans++;
			solved = DoIterationBackward(env, originGoal, originGoal, 0, midState);
		}
		else if (!solved && scanStats.backwardScans == backwardScansBefore)
		{
			onlyForwardEmpty = true;
			scanStats.onlyForwardEmptyCycles++;
		}
		backwardRan = scanStats.backwardScans > backwardScansBefore;
		iterationStats.push_back({
			scanStats.boundCycles,
			fBound,
			forwardBound,
			fBound - forwardBound - smallestEdge,
			forwardExpandedInLastIter - forwardExpandedBefore,
			backwardExpandedInLastIter - backwardExpandedBefore,
			nodesExpanded,
			forwardCandidates,
			backwardRan,
			onlyForwardEmpty,
			solved,
			scanStats.forwardScans - forwardScansBeforeCycle,
			scanStats.backwardScans - backwardScansBeforeCycle,
			scanStats.backwardTableFullScans - tableFullScansBeforeCycle,
			scanStats.backwardEndOfCycleScans - endCycleScansBeforeCycle
		});
		if (solved)
		{
			dMMExpansions = previousIterationExpansions + dMMLastIterExpansions;
			necessaryExpansions += nodesExpandedSoFar;
			return true;
		}
		else
		{
			transTable.clear();
			hashTable.clear();

			if (!isUpdateByWorkload)
			{
				forwardBound = nextBound / 2 - smallestEdge;
			}
			else
			{
				forwardBound = calculateBoundByWorkload(nextBound, fBound, forwardBound, forwardExpandedInLastIter, backwardExpandedInLastIter);
			}
			fBound = nextBound;
			forwardExpandedInLastIter = 0;
			backwardExpandedInLastIter = 0;
		}
		previousIterationExpansions = nodesExpanded - nodesExpandedSoFar;
		nodesExpandedSoFar = nodesExpanded;
	}
	return false;
}

template <class state, class action, bool verbose, class table>
bool IDTHSwTrans<state, action, verbose, table>::DoIterationForward(SearchEnvironment<state, action> *env,
																		state parent, state currState, uint64_t hashValue, double g, uint64_t &midState)
{
	LevelStat &level = GetLevelStat(true, g);
	level.visits++;
	double h = env->HCost(currState, originGoal);
	if (fgreater(g + h, fBound))
	{
		level.basePruned++;
		UpdateNextBound(g + h);
		return false;
	}

	if (g > forwardBound)
	{
		// Store the exact error even in an err0 run. It is inert unless the
		// consistency feature is enabled, but permits a faithful local shadow
		// test in the backward scan.
		double error = g - env->HCost(currState, originStart);
		transTable.push_back(transpostionNode<state>(currState, g, h, error));
		level.frontierInserted++;
		if (useHash)
		{
			hashTable[hashValue] = g; // hashing to element list location
		}
		if (transTable.size() > availableStorage)
		{
			scanStats.backwardScans++;
			scanStats.backwardTableFullScans++;
			bool isSolutionFound = DoIterationBackward(env, originGoal, originGoal, 0, midState);
			if (isSolutionFound)
			{
				return true;
			}
			else
			{
				transTable.clear();
				hashTable.clear();
				return false;
			}
		}
		return false;
	}
	std::vector<state> neighbors;
	env->GetSuccessors(currState, neighbors);
	nodesTouched += neighbors.size();
	nodesGenerated += neighbors.size();
	nodesExpanded++;
	forwardExpandedInLastIter++;
	level.expanded++;
	level.generated += neighbors.size();
	if (g + h == fBound)
	{
		dMMLastIterExpansions++;
	}
	for (unsigned int x = 0; x < neighbors.size(); x++)
	{
		double childG;
		double edgeCost = env->GCost(currState, neighbors[x]);
		if (neighbors[x] == parent)
		{
			level.duplicatePruned++;
			continue;
		}
		uint64_t hash;
		if (useHash)
		{
			hash = env->GetStateHash(neighbors[x]);
			typename IndexTable::const_iterator it;
			it = hashTable.find(hash);
			if (it != hashTable.end())
			{
				childG = (*it).second;
				if (childG <= g + edgeCost)
				{
					level.duplicatePruned++;
					continue;
				}
				else
				{
					hashTable[hash] = g + edgeCost;
				}
			}
		}
		level.accepted++;
		if (DoIterationForward(env, currState, neighbors[x], hash, g + edgeCost, midState))
		{
			return true;
		}
	}
	return false;
}

template <class state, class action, bool verbose, class table>
bool IDTHSwTrans<state, action, verbose, table>::DoIterationBackward(SearchEnvironment<state, action> *env,
																		 state parent, state currState, double g, uint64_t &midState)
{
	LevelStat &level = GetLevelStat(false, g);
	level.visits++;
	nodesTouched++;
	double fPossibleBound = 0;
	std::vector<transpostionNode<state>> ignoreList;
	for (uint64_t i = transTable.size() - 1; i != static_cast<uint64_t>(-1); i--)
	{
		state &possibleMidState = transTable[i].data;
		double possibleMidStateG = transTable[i].g;
		double otherError = transTable[i].error;
		double otherH = transTable[i].h;
		level.candidateChecks++;
		backwardBound = fBound - possibleMidStateG - smallestEdge;
		if (currState == possibleMidState && !fgreater(g + possibleMidStateG, fBound))
		{
			pathLength = g + possibleMidStateG;
			midState = i;
			return true;
		}
		double computedF = 0;
		double originalH = env->HCost(currState, originStart);
		const double error = g - env->HCost(currState, originGoal);
		const double baseForwardTerm = possibleMidStateG + otherH;
		const double baseBackwardTerm = g + originalH;
		const double baseF = std::max(baseForwardTerm, baseBackwardTerm);
		const double consistencyF = std::max(possibleMidStateG + otherH + error,
		                                     g + originalH + otherError);
		double f2fTerm = 0;
		if (front2frontH || collectF2FShadow)
		{
			f2fTerm = g + possibleMidStateG + env->HCost(currState, possibleMidState);
		}

		computedF = useConsistentErrorBound ? consistencyF : baseF;
		if (front2frontH)
			computedF = std::max(computedF, f2fTerm);

		const bool depthPruned = g > backwardBound;
		const bool actualPruned = fgreater(computedF, fBound);
		const bool f2fCondition = (front2frontH || collectF2FShadow) &&
		                          fgreater(f2fTerm, fBound);
		const bool consistencyCondition = fgreater(consistencyF, fBound);

		if (actualPruned || depthPruned)
		{
			level.candidatesPruned++;
			if (depthPruned)
				level.directionDepthPruned++;
			if (!depthPruned && !fgreater(baseF, fBound))
			{
				const bool byF2F = front2frontH && f2fCondition;
				const bool byConsistency = useConsistentErrorBound && consistencyCondition;
				if (byF2F) level.actualF2FPruned++;
				if (byConsistency) level.actualConsistencyPruned++;
				if (byF2F && byConsistency) level.actualFeatureOverlap++;
			}
			ignoreList.push_back(transTable[i]);
			transTable[i] = transTable.back();
			transTable.pop_back();
			UpdateNextBound(computedF);
		}
		else
		{
			// Local counterfactuals are evaluated only for candidates retained by
			// the active configuration. They therefore measure immediate pruning
			// opportunities, not end-to-end expansion savings.
			if (!front2frontH && collectF2FShadow &&
				fgreater(std::max(computedF, f2fTerm), fBound))
				level.shadowF2FWouldPrune++;
			if (!useConsistentErrorBound &&
				fgreater(std::max(computedF, consistencyF), fBound))
				level.shadowConsistencyWouldPrune++;
			if (!front2frontH && !useConsistentErrorBound && collectF2FShadow &&
				fgreater(std::max(f2fTerm, consistencyF), fBound))
				level.shadowBothWouldPrune++;
		}
	}

	if (transTable.size() > 0)
	{
		std::vector<state> neighbors;
		env->GetSuccessors(currState, neighbors);
		nodesExpanded++;
		nodesGenerated += neighbors.size();
		backwardExpandedInLastIter++;
		level.expanded++;
		level.generated += neighbors.size();
		if (fPossibleBound == fBound)
		{
			dMMLastIterExpansions++;
		}
		for (unsigned int x = 0; x < neighbors.size(); x++)
		{
			double edgeCost = env->GCost(currState, neighbors[x]);
			if (neighbors[x] == parent)
			{
				level.duplicatePruned++;
				continue;
			}
			level.accepted++;
			if (DoIterationBackward(env, currState, neighbors[x], g + edgeCost, midState))
			{
				return true;
			}
		}
	}

	transTable.insert(transTable.end(), ignoreList.begin(), ignoreList.end());
	ignoreList.clear();
	return false;
}

template <class state, class action, bool verbose, class table>
void IDTHSwTrans<state, action, verbose, table>::UpdateNextBound(double fCost)
{
	if (!fgreater(nextBound, fBound))
	{
		nextBound = std::max(fCost, fBound);
	}
	else if (fgreater(fCost, fBound) && fless(fCost, nextBound))
	{
		nextBound = fCost;
	}
}

#endif
