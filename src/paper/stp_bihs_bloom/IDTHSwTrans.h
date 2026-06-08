/*
 *  IDTHSwTrans.h
 *
 *  Created by Steven Danishevski on MARCH 20.
 *
 */

#ifndef IDTHS_TRANS_H
#define IDTHS_TRANS_H

#include <iostream>
#include <unordered_set>
#include "SearchEnvironment.h"
#include <math.h>

template <typename state>
class transpostionNode
{
public:
	transpostionNode(const state &theData, double gCost, double hCost, double error) : data(theData), error(error), g(gCost), h(hCost) {}
	state data;
	double g;
	double h;
	double error;
};

template <class state, class action, bool verbose = true, class table = std::vector<transpostionNode<state>>>
class IDTHSwTrans
{
public:
	IDTHSwTrans(bool front2frontH = false, bool isConsistent = false, bool isUpdateByWorkload = false, double smallestEdge = 1, bool useHash = true)
	{
		this->front2frontH = front2frontH;
		this->isConsistent = isConsistent;
		this->smallestEdge = smallestEdge;
		this->isUpdateByWorkload = isUpdateByWorkload;
		this->useHash = useHash;
	}
	virtual ~IDTHSwTrans() {}
	bool GetPath(SearchEnvironment<state, action> *env, state fromState, state toState, int secondsLimit = 600, unsigned long availableStorage = 0);
	double getPathLength() { return pathLength; }
	uint64_t GetNodesExpanded() { return nodesExpanded; }
	uint64_t GetNecessaryExpansions() { return necessaryExpansions; }
	uint64_t GetNodesTouched() { return nodesTouched; }
	void ResetNodeCount() { nodesExpanded = nodesTouched = 0; }
	unsigned long getDMMExpansions() { return dMMExpansions; }

private:
	unsigned long nodesExpanded, nodesTouched, dMMExpansions;
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
	bool isConsistent;
	double smallestEdge;
	bool isUpdateByWorkload;
	std::chrono::steady_clock::time_point startTimeTest;
};

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
														 state fromState, state toState, int secondsLimit, unsigned long availableStorage)
{
	this->availableStorage = availableStorage;
	auto startTime = std::chrono::steady_clock::now();
	nodesExpanded = nodesTouched = 0;
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
		dMMLastIterExpansions = 0;
		auto currentTime = std::chrono::steady_clock::now();
		std::chrono::duration<double> elapsed_seconds = currentTime - startTime;
		if (elapsed_seconds.count() >= secondsLimit)
		{
			return false;
		}
		if (verbose)
		{
			printf("\tBounds: %1.1f(%1.1f, %1.1f): ", fBound, forwardBound, backwardBound);
		}
		uint64_t hash;
		if (useHash)
		{
			hash = env->GetStateHash(originStart);
		}
		bool solved = DoIterationForward(env, originStart, originStart, hash, 0, midState);
		if (verbose)
		{
			printf("Nodes expanded: %d(%d)\n", nodesExpanded - nodesExpandedSoFar, nodesExpanded);
		}

		if (!solved && transTable.size() > 0)
		{
			solved = DoIterationBackward(env, originGoal, originGoal, 0, midState);
		}
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
	double h = env->HCost(currState, originGoal);
	if (fgreater(g + h, fBound))
	{
		UpdateNextBound(g + h);
		return false;
	}

	if (g > forwardBound)
	{
		double error = 0;
		if (isConsistent)
		{
			error = g - env->HCost(currState, originStart);
		}
		transTable.push_back(transpostionNode<state>(currState, g, h, error));
		if (useHash)
		{
			hashTable[hashValue] = g; // hashing to element list location
		}
		if (transTable.size() > availableStorage)
		{
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
	nodesExpanded++;
	forwardExpandedInLastIter++;
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
					continue;
				}
				else
				{
					hashTable[hash] = g + edgeCost;
				}
			}
		}
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
	nodesTouched++;
	double fPossibleBound = 0;
	std::vector<transpostionNode<state>> ignoreList;
	for (uint64_t i = transTable.size() - 1; i != static_cast<uint64_t>(-1); i--)
	{
		state &possibleMidState = transTable[i].data;
		double possibleMidStateG = transTable[i].g;
		double otherError = transTable[i].error;
		double otherH = transTable[i].h;
		backwardBound = fBound - possibleMidStateG - smallestEdge;
		if (currState == possibleMidState && !fgreater(g + possibleMidStateG, fBound))
		{
			pathLength = g + possibleMidStateG;
			midState = i;
			return true;
		}
		double computedF = 0;
		double error = 0;
		double originalH = 0;
		if (isConsistent)
		{
			error = g - env->HCost(currState, originGoal);
			originalH = env->HCost(currState, originStart);
		}
		if (front2frontH)
		{
			computedF = g + possibleMidStateG + env->HCost(currState, possibleMidState);
		}
		computedF = std::max(computedF, possibleMidStateG + otherH + error);
		computedF = std::max(computedF, g + originalH + otherError);
		if (fgreater(computedF, fBound) || g > backwardBound)
		{
			ignoreList.push_back(transTable[i]);
			transTable[i] = transTable.back();
			transTable.pop_back();
			UpdateNextBound(computedF);
		}
	}

	if (transTable.size() > 0)
	{
		std::vector<state> neighbors;
		env->GetSuccessors(currState, neighbors);
		nodesExpanded++;
		backwardExpandedInLastIter++;
		if (fPossibleBound == fBound)
		{
			dMMLastIterExpansions++;
		}
		for (unsigned int x = 0; x < neighbors.size(); x++)
		{
			uint64_t childID;
			double edgeCost = env->GCost(currState, neighbors[x]);
			if (neighbors[x] != parent && DoIterationBackward(env, currState, neighbors[x], g + edgeCost, midState))
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