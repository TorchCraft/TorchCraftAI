//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#include "graph.h"
#include "mapImpl.h"
#include "neutral.h"
#include <map>
#include <deque>


using namespace BWAPI;
using namespace BWAPI::UnitTypes::Enum;

using namespace std;


namespace BWEM {
namespace detail {


Area * mainArea(MapImpl * pMap, TilePosition topLeft, TilePosition size)
{
	map<Area *, int> map_Area_freq;

	for (int dy = 0 ; dy < size.y ; ++dy)
	for (int dx = 0 ; dx < size.x ; ++dx)
		if (Area * area = pMap->GetArea(topLeft + TilePosition(dx, dy)))
			++map_Area_freq[area];

	return map_Area_freq.empty() ? nullptr : map_Area_freq.rbegin()->first;
}





//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Graph
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////


const Area * Graph::GetArea(WalkPosition w) const
{
	Area::id id = GetMap()->GetMiniTile(w).AreaId();
	return id > 0 ? GetArea(id) : nullptr;
}



const Area * Graph::GetArea(TilePosition t) const
{
	Area::id id = GetMap()->GetTile(t).AreaId();
	return id > 0 ? GetArea(id) : nullptr;
}


const vector<ChokePoint> & Graph::GetChokePoints(Area::id a, Area::id b) const
{ 
	bwem_assert(Valid(a)); 
	bwem_assert(Valid(b)); 
	bwem_assert(a != b); 
	
	if (a > b) swap(a, b);
	
	return m_ChokePointsMatrix[b][a];
}


void Graph::CreateAreas(const vector<pair<WalkPosition, int>> & AreasList)
{
	m_Areas.reserve(AreasList.size());
	for (Area::id id = 1 ; id <= (Area::id)AreasList.size() ; ++id)
	{
		WalkPosition top = AreasList[id-1].first;
		int miniTiles = AreasList[id-1].second;
		m_Areas.emplace_back(this, id, top, miniTiles);
	}
}


void Graph::CreateChokePoints()
{
	ChokePoint::index newIndex = 0;

	vector<Neutral *> BlockingNeutrals;
	for (auto & s : GetMap()->StaticBuildings())		if (s->Blocking()) BlockingNeutrals.push_back(s.get());
	for (auto & m : GetMap()->Minerals())			if (m->Blocking()) BlockingNeutrals.push_back(m.get());

	const int pseudoChokePointsToCreate = count_if(BlockingNeutrals.begin(), BlockingNeutrals.end(),
											[](const Neutral * n){ return !n->NextStacked(); });

	// 1) Size the matrix
	m_ChokePointsMatrix.resize(AreasCount() + 1);
	for (Area::id id = 1 ; id <= AreasCount() ; ++id)
		m_ChokePointsMatrix[id].resize(id);			// triangular matrix

	// 2) Dispatch the global raw frontier between all the relevant pairs of Areas:
	map<pair<Area::id, Area::id>, vector<WalkPosition>> RawFrontierByAreaPair;
	for (const auto & raw : GetMap()->RawFrontier())
	{
		Area::id a = raw.first.first;
		Area::id b = raw.first.second;
		if (a > b) swap(a, b);
		bwem_assert(a <= b);
		bwem_assert((a >= 1) && (b <= AreasCount()));

		RawFrontierByAreaPair[make_pair(a, b)].push_back(raw.second);
	}

	// 3) For each pair of Areas (A, B):
	for (auto & raw : RawFrontierByAreaPair)
	{
		Area::id a = raw.first.first;
		Area::id b = raw.first.second;

		const vector<WalkPosition> & RawFrontierAB = raw.second;

		// Because our dispatching preserved order,
		// and because Map::m_RawFrontier was populated in descending order of the altitude (see Map::ComputeAreas),
		// we know that RawFrontierAB is also ordered the same way, but let's check it:
		{
			vector<altitude_t> Altitudes;
			for (auto w : RawFrontierAB)
				Altitudes.push_back(GetMap()->GetMiniTile(w).Altitude());

			bwem_assert(is_sorted(Altitudes.rbegin(), Altitudes.rend()));
		}

		// 3.1) Use that information to efficiently cluster RawFrontierAB in one or several chokepoints.
		//    Each cluster will be populated starting with the center of a chokepoint (max altitude)
		//    and finishing with the ends (min altitude).
		const int cluster_min_dist = (int)sqrt(lake_max_miniTiles);
		vector<deque<WalkPosition>> Clusters;
		for (auto w : RawFrontierAB)
		{
			bool added = false;
			for (auto & Cluster : Clusters)
			{
				int distToFront = queenWiseDist(Cluster.front(), w);
				int distToBack = queenWiseDist(Cluster.back(), w);
				if (min(distToFront, distToBack) <= cluster_min_dist)
				{
					if (distToFront < distToBack)	Cluster.push_front(w);
					else							Cluster.push_back(w);

					added = true;
					break;
				}
			}

			if (!added) Clusters.push_back(deque<WalkPosition>(1, w));
		}

		// 3.2) Create one Chokepoint for each cluster:
		GetChokePoints(a, b).reserve(Clusters.size() + pseudoChokePointsToCreate);
		for (const auto & Cluster : Clusters)
			GetChokePoints(a, b).emplace_back(this, newIndex++, GetArea(a), GetArea(b), Cluster);
	}

	// 4) Create one Chokepoint for each pair of blocked areas, for each blocking Neutral:
	

	for (Neutral * pNeutral : BlockingNeutrals)
		if (!pNeutral->NextStacked())		// in the case where several neutrals are stacked, we only consider the top
		{
			vector<const Area *> BlockedAreas = pNeutral->BlockedAreas();
			for (const Area * pA : BlockedAreas)
			for (const Area * pB : BlockedAreas)
			{
				if (pB == pA) break;	// breaks symmetry

				auto center = GetMap()->BreadthFirstSearch(WalkPosition(pNeutral->Pos()),
						[](const MiniTile & miniTile, WalkPosition) { return miniTile.Walkable(); },	// findCond
						[](const MiniTile &,          WalkPosition) { return true; });					// visitCond

				GetChokePoints(pA, pB).reserve(pseudoChokePointsToCreate);
				GetChokePoints(pA, pB).emplace_back(this, newIndex++, pA, pB, deque<WalkPosition>(1, center), pNeutral);
			}
		}

	// 5) Set the references to the freshly created Chokepoints:
	for (Area::id a = 1 ; a <= AreasCount() ; ++a)
	for (Area::id b = 1 ; b < a ; ++b)
		if (!GetChokePoints(a, b).empty())
		{
			GetArea(a)->AddChokePoints(GetArea(b), &GetChokePoints(a, b));
			GetArea(b)->AddChokePoints(GetArea(a), &GetChokePoints(a, b));

			for (auto & cp : GetChokePoints(a, b))
				m_ChokePointList.push_back(&cp);
		}
}


void Graph::SetDistance(const ChokePoint * cpA, const ChokePoint * cpB, int value)
{
	m_ChokePointDistanceMatrix[cpA->Index()][cpB->Index()] =
	m_ChokePointDistanceMatrix[cpB->Index()][cpA->Index()] = value;
}


void Graph::SetPath(const ChokePoint * cpA, const ChokePoint * cpB, const CPPath & PathAB)
{
	m_PathsBetweenChokePoints[cpA->Index()][cpB->Index()] = PathAB;
	m_PathsBetweenChokePoints[cpB->Index()][cpA->Index()].assign(PathAB.rbegin(), PathAB.rend());
}


// Computes the ground distances between any pair of ChokePoints in pContext
// This is achieved by invoking several times pContext->ComputeDistances,
// which effectively computes the distances from one starting ChokePoint, using Dijkstra's algorithm.
// If Context == Area, Dijkstra's algorithm works on the Tiles inside one Area.
// If Context == Graph, Dijkstra's algorithm works on the GetChokePoints between the AreaS.
template<class Context>
void Graph::ComputeChokePointDistances(const Context * pContext)
{
///	multimap<int, vector<WalkPosition>> trace;

	for (const ChokePoint * pStart : pContext->ChokePoints())
	{
		vector<const ChokePoint *> Targets;
		for (const ChokePoint * cp : pContext->ChokePoints())
		{
			if (cp == pStart) break;	// breaks symmetry
			Targets.push_back(cp);
		}

		auto DistanceToTargets = pContext->ComputeDistances(pStart, Targets);

		for (int i = 0 ; i < (int)Targets.size() ; ++i)
		{
			int newDist = DistanceToTargets[i];
			int existingDist = Distance(pStart, Targets[i]);

			if (newDist && ((existingDist == -1) || (newDist < existingDist)))
			{
				SetDistance(pStart, Targets[i], newDist);

				// Build the path from pStart to Targets[i]:

				CPPath Path {pStart, Targets[i]};

				// if (Context == Graph), there may be intermediate ChokePoints. They have been set by ComputeDistances,
				// so we just have to collect them (in the reverse order) and insert them into Path:
				if ((void *)(pContext) == (void *)(this))	// tests (Context == Graph) without warning about constant condition
					for (const ChokePoint * pPrev = Targets[i]->PathBackTrace() ; pPrev != pStart ; pPrev = pPrev->PathBackTrace())
						Path.insert(Path.begin()+1, pPrev);

				SetPath(pStart, Targets[i], Path);

			///	vector<WalkPosition> PathTrace;
			///	for (auto e : Path) PathTrace.push_back(e->Center());
			///	trace.emplace(int(0.5 + DistanceToTargets[i]/8.0), PathTrace);
			}
		}
	}

///	for (auto & line : trace) { Log << line.first; for (auto e : line.second) Log << " " << e; Log << endl; }

}

template void Graph::ComputeChokePointDistances<Graph>(const Graph * pContext);
template void Graph::ComputeChokePointDistances<Area>(const Area * pContext);


void Graph::ComputeChokePointDistanceMatrix()
{
	// 1) Size the matrix
	m_ChokePointDistanceMatrix.clear();
	m_ChokePointDistanceMatrix.resize(m_ChokePointList.size());
	for (auto & line : m_ChokePointDistanceMatrix)
		line.resize(m_ChokePointList.size(), -1);

	m_PathsBetweenChokePoints.clear();
	m_PathsBetweenChokePoints.resize(m_ChokePointList.size());
	for (auto & line : m_PathsBetweenChokePoints)
		line.resize(m_ChokePointList.size());

	// 2) Compute distances inside each Area
	for (const Area & area : Areas())
		ComputeChokePointDistances(&area);

	// 3) Compute distances through connected Areas
	ComputeChokePointDistances(this);

	for (const ChokePoint * cp : ChokePoints())
	{
		SetDistance(cp, cp, 0);
		SetPath(cp, cp, CPPath{cp});
	}

	// 4) Update Area::m_AccessibleNeighbours for each Area
	for (Area & area : Areas())
		area.UpdateAccessibleNeighbours();

	// 5)  Update Area::m_groupId for each Area
	UpdateGroupIds();
}


// Returns Distances such that Distances[i] == ground_distance(start, Targets[i]) in pixels
// Any Distances[i] may be 0 (meaning Targets[i] is not reachable).
// This may occur in the case where start and Targets[i] leave in different continents or due to Bloqued intermediate ChokePoint(s).
// For each reached target, the shortest path can be derived using
// the backward trace set in cp->PathBackTrace() for each intermediate ChokePoint cp from the target.
// Note: same algo than Area::ComputeDistances (derived from Dijkstra)
vector<int> Graph::ComputeDistances(const ChokePoint * start, const vector<const ChokePoint *> & Targets) const
{
	const MapImpl * pMap = GetMap();
	vector<int> Distances(Targets.size());

	pMap->UnmarkAllTiles();

	multimap<int, const ChokePoint *> ToVisit;	// a priority queue holding the GetChokePoints to visit ordered by their distance to start.
	ToVisit.emplace(0, start);

	int remainingTargets = Targets.size();
	while (!ToVisit.empty())
	{
		int currentDist = ToVisit.begin()->first;
		const ChokePoint * current = ToVisit.begin()->second;
		const Tile & currentTile = pMap->GetTile(TilePosition(current->Center()), check_t::no_check);
		bwem_assert(currentTile.InternalData() == currentDist);
		ToVisit.erase(ToVisit.begin());
		currentTile.SetInternalData(0);										// resets Tile::m_internalData for future usage
		pMap->SetTileMarked(currentTile);

		for (int i = 0 ; i < (int)Targets.size() ; ++i)
			if (current == Targets[i])
			{
				Distances[i] = currentDist;
				--remainingTargets;
			}
		if (!remainingTargets) break;

		if (current->Blocked() && (current != start)) continue;

		for (const Area * pArea : {current->GetAreas().first, current->GetAreas().second})
			for (const ChokePoint * next : pArea->ChokePoints())
				if (next != current)
				{
					const int newNextDist = currentDist + Distance(current, next);
					const Tile & nextTile = pMap->GetTile(TilePosition(next->Center()), check_t::no_check); 
					if (!pMap->IsTileMarked(nextTile))
					{
						if (nextTile.InternalData())	// next already in ToVisit
						{
							if (newNextDist < nextTile.InternalData())		// nextNewDist < nextOldDist
							{	// To update next's distance, we need to remove-insert it from ToVisit:
								auto range = ToVisit.equal_range(nextTile.InternalData());
								auto iNext = find_if(range.first, range.second, [next]
									(const pair<int, const ChokePoint *> & e) { return e.second == next; });
								bwem_assert(iNext != range.second);

								ToVisit.erase(iNext);
								nextTile.SetInternalData(newNextDist);
								next->SetPathBackTrace(current);
								ToVisit.emplace(newNextDist, next);
							}
						}
						else
						{
							nextTile.SetInternalData(newNextDist);
							next->SetPathBackTrace(current);
							ToVisit.emplace(newNextDist, next);
						}
					}
				}
	}

//	bwem_assert(!remainingTargets);

	// Reset Tile::m_internalData for future usage
	for (auto e : ToVisit)
		pMap->GetTile(TilePosition(e.second->Center()), check_t::no_check).SetInternalData(0);	
	
	return Distances;
}


const CPPath & Graph::GetPath(const Position & a, const Position & b, int * pLength) const
{
	const Area * pAreaA = GetNearestArea(WalkPosition(a));
	const Area * pAreaB = GetNearestArea(WalkPosition(b));

	if (pAreaA == pAreaB)
	{
		if (pLength) *pLength = a.getApproxDistance(b);
		return m_EmptyPath;
	};
		
	if (!pAreaA->AccessibleFrom(pAreaB))
	{
		if (pLength) *pLength = -1;
		return m_EmptyPath;
	};

	int minDist_A_B = numeric_limits<int>::max();

	const ChokePoint * pBestCpA = nullptr;
	const ChokePoint * pBestCpB = nullptr;

	for (const ChokePoint * cpA : pAreaA->ChokePoints()) if (!cpA->Blocked())
	{
		const int dist_A_cpA = a.getApproxDistance(Position(cpA->Center()));
		for (const ChokePoint * cpB : pAreaB->ChokePoints()) if (!cpB->Blocked())
		{
			const int dist_B_cpB = b.getApproxDistance(Position(cpB->Center()));
			const int dist_A_B = dist_A_cpA + dist_B_cpB + Distance(cpA, cpB);
			if (dist_A_B < minDist_A_B)
			{
				minDist_A_B = dist_A_B;
				pBestCpA = cpA;
				pBestCpB = cpB;
			}
		}
	}

	bwem_assert(minDist_A_B != numeric_limits<int>::max());

	const CPPath & Path = GetPath(pBestCpA, pBestCpB);

	if (pLength)
	{
		bwem_assert(Path.size() >= 1);

		*pLength = minDist_A_B;

		if (Path.size() == 1)
		{
			bwem_assert(pBestCpA == pBestCpB);
			const ChokePoint * cp = pBestCpA;

			Position cpEnd1 = center(cp->Pos(ChokePoint::end1));
			Position cpEnd2 = center(cp->Pos(ChokePoint::end2));
			if (intersect(a.x, a.y, b.x, b.y, cpEnd1.x, cpEnd1.y, cpEnd2.x, cpEnd2.y))
				*pLength = a.getApproxDistance(b);
			else
				for (ChokePoint::node node : {ChokePoint::end1, ChokePoint::end2})
				{
					Position c = center(cp->Pos(node));
					int dist_A_B = a.getApproxDistance(c) + b.getApproxDistance(c);
					if (dist_A_B < *pLength) *pLength = dist_A_B;
				}
		}
	}

	return GetPath(pBestCpA, pBestCpB);
}


void Graph::UpdateGroupIds()
{
	Area::groupId nextGroupId = 1;

	UnmarkAllAreas();
	for (Area & start : Areas())
		if (!IsAreaMarked(start))
		{
			vector<Area *> ToVisit{&start};
			while (!ToVisit.empty())
			{
				Area * current = ToVisit.back();
				ToVisit.pop_back();
				current->SetGroupId(nextGroupId);

				for (const Area * next : current->AccessibleNeighbours())
					if (!IsAreaMarked(*next))
					{
						SetAreaMarked(*next);
						ToVisit.push_back(const_cast<Area *>(next));
					}
			}
			++nextGroupId;
		}
}


void Graph::CollectInformation()
{
	// 1) Process the whole Map:

	for (auto & m : GetMap()->Minerals())
		if (Area * pArea = mainArea(GetMap(), m->TopLeft(), m->Size()))
			pArea->AddMineral(m.get());

	for (auto & g : GetMap()->Geysers())	
		if (Area * pArea = mainArea(GetMap(), g->TopLeft(), g->Size()))
			pArea->AddGeyser(g.get());

	for (int y = 0 ; y < GetMap()->Size().y ; ++y)
	for (int x = 0 ; x < GetMap()->Size().x ; ++x)
	{
		const Tile & tile = GetMap()->GetTile(TilePosition(x, y));
		if (tile.AreaId() > 0)
			GetArea(tile.AreaId())->AddTileInformation(TilePosition(x, y), tile);
	}

	// 2) Post-process each Area separately:

	for (Area & area : m_Areas)
		area.PostCollectInformation();
}


void Graph::CreateBases()
{
	m_baseCount = 0;
	for (Area & area : m_Areas)
	{
		area.CreateBases();
		m_baseCount += area.Bases().size();
	}
}

const Area * Graph::GetNearestArea(BWAPI::WalkPosition p) const
{
  typedef BWAPI::WalkPosition TPosition;
	typedef TileOfPosition<TPosition>::type Tile_t;
	if (const Area * area = GetArea(p)) return area;

	p = GetMap()->BreadthFirstSearch(p,
					[this](const Tile_t & t, TPosition) { return t.AreaId() > 0; },	// findCond
					[](const Tile_t &,       TPosition) { return true; });			// visitCond

	return GetArea(p);
}

const Area * Graph::GetNearestArea(BWAPI::TilePosition p) const
{
  typedef BWAPI::TilePosition TPosition;
	typedef TileOfPosition<TPosition>::type Tile_t;
	if (const Area * area = GetArea(p)) return area;

	p = GetMap()->BreadthFirstSearch(p,
					[this](const Tile_t & t, TPosition) { return t.AreaId() > 0; },	// findCond
					[](const Tile_t &,       TPosition) { return true; });			// visitCond

	return GetArea(p);
}
	
}} // namespace BWEM::detail



