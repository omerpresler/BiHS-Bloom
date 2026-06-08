/*
 *  IDBiHS.h
 *
 *  Created by Steven Danishevski on MARCH 20.
 *
 */

#ifndef IDBiHS_H
#define IDBiHS_H

#include <iostream>
#include <unordered_set>
#include "SearchEnvironment.h"
#include "AStarOpenClosed.h"
#include "TemplateAStar.h"
#include "MM.h"
#include <math.h>
#include <typeinfo>

template <class environment, class state, class action, bool verbose = false>
class IDBiHS
{
	typedef AStarOpenClosed<state, AStarCompareWithF<state>, AStarOpenClosedDataWithF<state>> aStarStatesList;

public:
	IDBiHS(environment *env, bool F2Fheuristics = false, bool isConsistent = false, bool isUpdateByWorkload = false, double smallestEdge = 1)
	{
		this->env = env;
		this->F2Fheuristics = F2Fheuristics;
		this->isConsistent = isConsistent;
		this->smallestEdge = smallestEdge;
		this->isUpdateByWorkload = isUpdateByWorkload;
	}
	virtual ~IDBiHS() {}
	bool GetMidState(state fromState, state toState, state &midState, int secondsLimit = 600, double startingFBound = 0);
	bool Astar_plus_IDBiHS(state fromState, state toState, state &midState, unsigned long statesQuantityBound, int secondsLimit = 600, bool detectDuplicates = false);
	bool IDTHSpTrans(state fromState, state toState, state &midState, int secondsLimit = 600, unsigned long availableStorage = 0);
	bool GetMidStateFromLists(state fromState, state toState, state &midState, int secondsLimit = 600, double startingFBound = 0, aStarStatesList &forwardList = aStarStatesList(), aStarStatesList &backwardList = aStarStatesList(), bool detectDuplicates = true);
	bool GetMidStateFromForwardList(state fromState, state toState, state &midState, int secondsLimit = 600, aStarStatesList &forwardList = aStarStatesList(), bool detectDuplicates = true);
	double getPathLength() { return pathLength; }
	uint64_t GetNodesExpanded() { return nodesExpanded; }
	uint64_t GetNecessaryExpansions() { return necessaryExpansions; }
	uint64_t GetNodesTouched() { return nodesTouched; }
	void ResetNodeCount() { nodesExpanded = nodesTouched = 0; }
	unsigned long getDMMExpansions() { return dMMExpansions; }

private:
	bool DoIterationForward(state parent, state currState, double g, state &midState);
	bool DoIterationBackward(state parent, state currState, double g, state &midState, state possibleMidState, double possibleMidStateG, double otherH = 0, double otherError = 0);
	double updateBoundByFraction(double boundToSplit, double p = 0.5, bool isInteger = true);
	double calculateBoundByWorkload(double newbound, double prevBound, double oldForwardBound, uint64_t forwardLoad, uint64_t backwardLoad);
	double calculateNextForwardBound();
	bool shouldSearchNeighbor(state &neighbor, double neighborG, state &parent, aStarStatesList &statesList);
	void UpdateNextBound(double fCost);

	environment *env;
	state originGoal, originStart;
	unsigned long nodesExpanded, nodesTouched, dMMExpansions;
	double backwardBound, forwardBound, fBound, nextBound;
	double pathLength = std::numeric_limits<double>::max();
	unsigned long dMMLastIterExpansions = 0;
	unsigned long necessaryExpansions = 0;
	unsigned long nodesExpandedSoFar = 0;
	unsigned long previousIterationExpansions = 0;
	unsigned long forwardExpandedInLastIter = 0;
	unsigned long backwardExpandedInLastIter = 0;
	unsigned long availableStorage = 0;

	aStarStatesList forwardList;
	aStarStatesList backwardList;
	std::vector<AStarOpenClosedDataWithF<state>> forwardOpenList;
	std::vector<AStarOpenClosedDataWithF<state>> backwardOpenList;
	bool readyOpenLists = false;
	bool F2Fheuristics, firstBounds, isConsistent, detectDuplicates, isUpdateByWorkload;
	double minForwardError = 0;
	double minBackwardError = 0;
	double smallestEdge;
	std::chrono::steady_clock::time_point startTimeTest;
};

template <class environment, class state, class action, bool verbose>
bool IDBiHS<environment, state, action, verbose>::Astar_plus_IDBiHS(state start, state goal, state &midState, unsigned long statesQuantityBound, int secondsLimit, bool detectDuplicates)
{
	TemplateAStar<state, action, environment> astar;
	std::vector<state> astarPath;
	Timer timer;
	timer.StartTimer();
	bool solved = astar.GetPathTime(this->env, start, goal, astarPath, secondsLimit, true, statesQuantityBound);
	nodesExpanded = astar.GetNodesExpanded();
	if (solved)
	{
		timer.EndTimer();
		necessaryExpansions = astar.GetNecessaryExpansions();
		pathLength = this->env->GetPathLength(astarPath);
	}
	else
	{
		solved = GetMidStateFromForwardList(start, goal, midState, secondsLimit - timer.GetElapsedTime(), astar.getStatesList(), detectDuplicates);
		timer.EndTimer();
	}
	nodesExpanded += astar.GetNodesExpanded();
	return solved;
}

template <class environment, class state, class action, bool verbose>
bool IDBiHS<environment, state, action, verbose>::GetMidStateFromForwardList(state fromState, state toState, state &midState, int secondsLimit, aStarStatesList &forwardList, bool detectDuplicates)
{
	aStarStatesList newBackwardList;
	double h = this->env->HCost(toState, fromState);
	newBackwardList.AddOpenNode(toState, this->env->GetStateHash(toState), 0 + h, 0, h);

	return GetMidStateFromLists(fromState, toState, midState, secondsLimit, 0, forwardList, newBackwardList, detectDuplicates);
}

template <class environment, class state, class action, bool verbose>
bool IDBiHS<environment, state, action, verbose>::GetMidStateFromLists(state fromState, state toState, state &midState, int secondsLimit, double startingFBound, aStarStatesList &forwardList, aStarStatesList &backwardList, bool detectDuplicates)
{
	auto startTime = std::chrono::steady_clock::now();
	this->readyOpenLists = true;
	this->forwardList = forwardList;
	this->backwardList = backwardList;
	this->detectDuplicates = detectDuplicates;
	nodesExpanded = nodesTouched = 0;
	originStart = fromState;
	originGoal = toState;
	double minF = std::numeric_limits<double>::max();
	double minFforward = std::numeric_limits<double>::max();
	double minFbackward = std::numeric_limits<double>::max();
	double maxOpenG = 0;
	double minOpenG = std::numeric_limits<double>::max();
	minForwardError = DBL_MAX;
	minBackwardError = DBL_MAX;

	for (int x = 0; x < forwardList.OpenSize(); x++)
	{
		AStarOpenClosedDataWithF<state> openState = forwardList.getElements()[forwardList.GetOpenItem(x)];
		minOpenG = std::min(minOpenG, openState.g);
		maxOpenG = std::max(maxOpenG, openState.g);
		minFforward = std::min(minFforward, openState.f);
		minForwardError = std::min(minForwardError, openState.g - this->env->HCost(originStart, openState.data));
		forwardOpenList.push_back(openState);
	}
	for (int x = 0; x < backwardList.OpenSize(); x++)
	{
		AStarOpenClosedDataWithF<state> openState = backwardList.getElements()[backwardList.GetOpenItem(x)];
		minFbackward = std::min(minFbackward, openState.f);
		minBackwardError = std::min(minBackwardError, openState.g - this->env->HCost(openState.data, originGoal));
		backwardOpenList.push_back(openState);
	}
	if (!isConsistent)
	{
		minBackwardError = minForwardError = 0;
	}
	minF = std::max(minFbackward, minFforward);
	sort(forwardOpenList.begin(), forwardOpenList.end(), [](const AStarOpenClosedDataWithF<state> &lhs, const AStarOpenClosedDataWithF<state> &rhs)
		 { return lhs.h < rhs.h; });
	sort(backwardOpenList.begin(), backwardOpenList.end(), [](const AStarOpenClosedDataWithF<state> &lhs, const AStarOpenClosedDataWithF<state> &rhs)
		 { return lhs.h < rhs.h; });

	double initialHeuristic = this->env->HCost(fromState, toState);
	fBound = nextBound = std::max(smallestEdge, std::max(startingFBound, std::max(initialHeuristic, minF)));
	forwardBound = std::max(minOpenG, ceil(fBound / 2) - smallestEdge);
	firstBounds = true;
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
			printf("\t\tBounds: %1.1f and %1.1f: ", forwardBound, backwardBound);
		}
		bool solved = false;
		for (AStarOpenClosedDataWithF<state> openState : forwardOpenList)
		{
			auto currentTime = std::chrono::steady_clock::now();
			std::chrono::duration<double> elapsed_seconds = currentTime - startTime;
			if (elapsed_seconds.count() >= secondsLimit)
			{
				return false;
			}
			solved = DoIterationForward(openState.data, openState.data, openState.g, midState);
			if (solved)
			{
				break;
			}
		}
		if (verbose)
		{
			printf("Nodes expanded: %d(%d)\n", nodesExpanded - nodesExpandedSoFar, nodesExpanded);
		}
		if (solved)
		{
			dMMExpansions = previousIterationExpansions + dMMLastIterExpansions;
			necessaryExpansions += nodesExpandedSoFar;
			for (AStarOpenClosedDataWithF<state> forwardState : forwardList.getElements())
			{
				if (forwardState.where == kClosedList && forwardState.g + forwardState.h < pathLength)
				{
					necessaryExpansions++;
				}
			}
			for (AStarOpenClosedDataWithF<state> backwardState : backwardList.getElements())
			{
				if (backwardState.where == kClosedList && backwardState.g + backwardState.h < pathLength)
				{
					necessaryExpansions++;
				}
			}
			return true;
		}
		else
		{
			forwardBound = calculateNextForwardBound();
			fBound = nextBound;
			forwardBound = std::max(minOpenG, forwardBound);
		}
	}
	return false;
}

template <class environment, class state, class action, bool verbose>
double IDBiHS<environment, state, action, verbose>::calculateNextForwardBound()
{
	if (!isUpdateByWorkload)
	{
		return ceil(nextBound / 2) - smallestEdge;
	}
	else
	{
		return calculateBoundByWorkload(nextBound, fBound, forwardBound, forwardExpandedInLastIter, backwardExpandedInLastIter);
	}
}

template <class environment, class state, class action, bool verbose>
double IDBiHS<environment, state, action, verbose>::updateBoundByFraction(double boundToSplit, double p, bool isInteger)
{
	if (isInteger)
	{
		return ceil(p * boundToSplit) - smallestEdge;
	}
	else
	{
		return p * boundToSplit - smallestEdge;
	}
}

template <class environment, class state, class action, bool verbose>
double IDBiHS<environment, state, action, verbose>::calculateBoundByWorkload(double newbound, double prevBound, double oldForwardBound, uint64_t forwardLoad, uint64_t backwardLoad)
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

template <class environment, class state, class action, bool verbose>
bool IDBiHS<environment, state, action, verbose>::shouldSearchNeighbor(state &neighbor, double neighborG, state &parent, aStarStatesList &statesList)
{
	uint64_t neighborID;
	return !(neighbor == parent || (detectDuplicates && statesList.getElements().size() > 1 && statesList.Lookup(this->env->GetStateHash(neighbor), neighborID) != kNotFound && (statesList.Lookup(neighborID).where == kClosedList || (statesList.Lookup(neighborID).where == kOpenList && statesList.Lookup(neighborID).g <= neighborG))));
}

template <class environment, class state, class action, bool verbose>
bool IDBiHS<environment, state, action, verbose>::GetMidState(state fromState, state toState, state &midState, int secondsLimit, double startingFBound)
{
	auto startTime = std::chrono::steady_clock::now();
	this->readyOpenLists = false;
	this->forwardList = forwardList;
	this->backwardList = backwardList;
	nodesExpanded = nodesTouched = 0;
	originStart = fromState;
	originGoal = toState;
	double initialHeuristic = this->env->HCost(fromState, toState);
	fBound = nextBound = std::max(smallestEdge, std::max(startingFBound, initialHeuristic));
	forwardBound = ceil(fBound / 2) - smallestEdge;
	unsigned long nodesExpandedSoFar = 0;
	unsigned long previousIterationExpansions = 0;
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
			printf("\t\tBounds: %1.1f and %1.1f: ", forwardBound, backwardBound);
		}
		bool solved = DoIterationForward(originStart, originStart, 0, midState);
		if (verbose)
		{
			printf("Nodes expanded: %d(%d)\n", nodesExpanded - nodesExpandedSoFar, nodesExpanded);
		}
		if (solved)
		{
			dMMExpansions = previousIterationExpansions + dMMLastIterExpansions;
			necessaryExpansions += nodesExpandedSoFar;
			return true;
		}
		else
		{
			forwardBound = calculateNextForwardBound();
			fBound = nextBound;
			forwardExpandedInLastIter = 0;
			backwardExpandedInLastIter = 0;
		}
		previousIterationExpansions = nodesExpanded - nodesExpandedSoFar;
		nodesExpandedSoFar = nodesExpanded;
	}
	return false;
}

template <class environment, class state, class action, bool verbose>
bool IDBiHS<environment, state, action, verbose>::DoIterationForward(state parent, state currState, double g, state &midState)
{
	double h = this->env->HCost(currState, originGoal);
	if (currState == originGoal && g <= fBound)
	{
		pathLength = g;
		midState = currState;
		return true;
	}
	if (fgreater(g + h + minBackwardError, fBound))
	{
		UpdateNextBound(g + h + minBackwardError);
		return false;
	}
	else if (g > forwardBound)
	{
		backwardBound = fBound - g - smallestEdge;
		double error = 0;
		if (isConsistent)
		{
			error = g - this->env->HCost(currState, originStart);
		}
		if (readyOpenLists)
		{
			for (AStarOpenClosedDataWithF<state> openState : backwardOpenList)
			{
				//if(openState.g + openState.h > fBound || openState.g > backwardBound){
				if (openState.g + openState.h > fBound)
				{
					UpdateNextBound(openState.g + openState.h);
					continue;
				}
				if (DoIterationBackward(openState.data, openState.data, openState.g, midState, currState, g, h, error))
				{
					pathLength += g;
					midState = currState;
					return true;
				}
			}
			return false;
		}
		else
		{
			bool solved = DoIterationBackward(originGoal, originGoal, 0, midState, currState, g, h, error);
			if (solved)
			{
				pathLength += g;
				midState = currState;
			}
			return solved;
		}
	}
	std::vector<state> neighbors;
	this->env->GetSuccessors(currState, neighbors);
	nodesTouched += neighbors.size();
	nodesExpanded++;
	forwardExpandedInLastIter++;
	if (g + h == fBound)
	{
		dMMLastIterExpansions++;
	}
	for (unsigned int x = 0; x < neighbors.size(); x++)
	{
		double neighborG = g + this->env->GCost(currState, neighbors[x]);
		if (shouldSearchNeighbor(neighbors[x], neighborG, parent, forwardList) && DoIterationForward(currState, neighbors[x], neighborG, midState))
		{
			return true;
		}
	}
	return false;
}

template <class environment, class state, class action, bool verbose>
bool IDBiHS<environment, state, action, verbose>::DoIterationBackward(state parent, state currState, double g, state &midState, state possibleMidState, double possibleMidStateG, double otherH, double otherError)
{
	if (g > backwardBound || (smallestEdge == 0 && g == backwardBound))
	{
		if (currState == possibleMidState && g + possibleMidStateG <= fBound)
		{
			pathLength = g;
			return true;
		}
		else if (g > backwardBound)
		{
			UpdateNextBound(g + possibleMidStateG + smallestEdge);
			return false;
		}
	}

	double originalH = this->env->HCost(currState, originStart);
	double fPossibleBound = g + originalH + otherError;

	if (F2Fheuristics)
	{
		double hToPossibleMidState = this->env->HCost(currState, possibleMidState);
		fPossibleBound = std::max(fPossibleBound, g + hToPossibleMidState + possibleMidStateG);
	}
	if (isConsistent)
	{
		double error = g - this->env->HCost(currState, originGoal);
		fPossibleBound = std::max(fPossibleBound, possibleMidStateG + otherH + error);
	}

	if (fgreater(fPossibleBound, fBound))
	{
		UpdateNextBound(fPossibleBound);
		return false;
	}

	std::vector<state> neighbors;
	this->env->GetSuccessors(currState, neighbors);
	nodesTouched += neighbors.size();
	nodesExpanded++;
	backwardExpandedInLastIter++;
	if (fPossibleBound == fBound)
	{
		dMMLastIterExpansions++;
	}
	for (unsigned int x = 0; x < neighbors.size(); x++)
	{
		double neighborG = g + this->env->GCost(currState, neighbors[x]);
		if (shouldSearchNeighbor(neighbors[x], neighborG, parent, backwardList) && DoIterationBackward(currState, neighbors[x], neighborG, midState, possibleMidState, possibleMidStateG, otherH, otherError))
		{
			return true;
		}
	}
	return false;
}

template <class environment, class state, class action, bool verbose>
void IDBiHS<environment, state, action, verbose>::UpdateNextBound(double fCost)
{
	if (!fgreater(nextBound, fBound))
	{
		nextBound = std::max(fBound, fCost);
	}
	else if (fgreater(fCost, fBound) && fless(fCost, nextBound))
	{
		nextBound = fCost;
	}
}

#endif
