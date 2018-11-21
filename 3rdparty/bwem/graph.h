//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_GRAPH_H
#define BWEM_GRAPH_H

#include "BWAPI.h"
#include "base.h"
#include "cp.h"
#include "area.h"
#include "bwapiExt.h"
#include "utils.h"
#include "defs.h"


namespace BWEM {

class Neutral;
class Mineral;
class Geyser;
class StaticBuilding;
class Tile;

namespace detail {

class MapImpl;

using namespace std;
using namespace utils;
using namespace BWAPI_ext;


//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Area
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//





//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Graph
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//

class Graph
{
public:
										Graph(MapImpl * pMap) : m_pMap(pMap) {}
	Graph &								operator=(const Graph &) = delete;

	const MapImpl *						GetMap() const					{ return m_pMap; }
	MapImpl *							GetMap()						{ return m_pMap; }

	const vector<Area> &				Areas() const					{ return m_Areas; }
	vector<Area> &						Areas()							{ return m_Areas; }
	int									AreasCount() const				{ return (int)m_Areas.size(); }

	const Area *						GetArea(Area::id id) const		{ bwem_assert(Valid(id)); return &m_Areas[id-1]; }
	Area *								GetArea(Area::id id)			{ bwem_assert(Valid(id)); return &m_Areas[id-1]; }
	
	const Area *						GetArea(BWAPI::WalkPosition w) const;
	Area *								GetArea(BWAPI::WalkPosition w)	{ return const_cast<Area *>(static_cast<const Graph &>(*this).GetArea(w)); }
	
	const Area *						GetArea(BWAPI::TilePosition t) const;
	Area *								GetArea(BWAPI::TilePosition t)	{ return const_cast<Area *>(static_cast<const Graph &>(*this).GetArea(t)); }

	const Area *						GetNearestArea(BWAPI::WalkPosition p) const;
	const Area *						GetNearestArea(BWAPI::TilePosition p) const;
	template<class TPosition> Area *	GetNearestArea(TPosition p)		{ return const_cast<Area *>(static_cast<const Graph &>(*this).GetNearestArea(p)); }


	// Returns the list of all the ChokePoints in the Map.
	const vector<ChokePoint *> &		ChokePoints() const				{ return m_ChokePointList; }

	// Returns the ChokePoints between two Areas.
	const vector<ChokePoint> &			GetChokePoints(Area::id a, Area::id b) const;
	const vector<ChokePoint> &			GetChokePoints(const Area * a, const Area * b) const	{ return GetChokePoints(a->Id(), b->Id()); }

	// Returns the ground distance in pixels between cpA->Center() and cpB>Center()
	int									Distance(const ChokePoint * cpA, const ChokePoint * cpB) const { return m_ChokePointDistanceMatrix[cpA->Index()][cpB->Index()]; }

	// Returns a list of ChokePoints, which is intended to be the shortest walking path from cpA to cpB.
	const CPPath &						GetPath(const ChokePoint * cpA, const ChokePoint * cpB) const { return m_PathsBetweenChokePoints[cpA->Index()][cpB->Index()]; }

	const CPPath &						GetPath(const BWAPI::Position & a, const BWAPI::Position & b, int * pLength = nullptr) const;

	int									BaseCount() const	{ return m_baseCount; }


	vector<ChokePoint> &				GetChokePoints(Area::id a, Area::id b)			{ return const_cast<vector<ChokePoint> &>(static_cast<const Graph &>(*this).GetChokePoints(a, b)); }
	vector<ChokePoint> &				GetChokePoints(const Area * a, const Area * b) 	{ return GetChokePoints(a->Id(), b->Id()); }

	// Creates a new Area for each pair (top, miniTiles) in AreasList (See Area::Top() and Area::MiniTiles())
	void								CreateAreas(const vector<pair<BWAPI::WalkPosition, int>> & AreasList);

	// Creates a new Area for each pair (top, miniTiles) in AreasList (See Area::Top() and Area::MiniTiles())
	void								CreateChokePoints();

	void								ComputeChokePointDistanceMatrix();

	void								CollectInformation();
	void								CreateBases();

private:
	template<class Context>
	void								ComputeChokePointDistances(const Context * pContext);
	vector<int>							ComputeDistances(const ChokePoint * pStartCP, const vector<const ChokePoint *> & TargetCPs) const;
	void								SetDistance(const ChokePoint * cpA, const ChokePoint * cpB, int value);
	void								UpdateGroupIds();
	void								SetPath(const ChokePoint * cpA, const ChokePoint * cpB, const CPPath & PathAB);
	bool								Valid(Area::id id) const			{ return (1 <= id) && (id <= AreasCount()); }

	// Tile marking
	void UnmarkAllAreas() { m_areaMark++; }
	void SetAreaMarked(const Area& area) { return area.SetMarked(m_areaMark);  }
	bool IsAreaMarked(const Area& area) { return area.IsMarkedWith(m_areaMark); }

	MapImpl * const						m_pMap;
	vector<Area>						m_Areas;
	vector<ChokePoint *>				m_ChokePointList;
	vector<vector<vector<ChokePoint>>>	m_ChokePointsMatrix;			// index == Area::id x Area::id
	vector<vector<int>>					m_ChokePointDistanceMatrix;		// index == ChokePoint::index x ChokePoint::index
	vector<vector<CPPath>>				m_PathsBetweenChokePoints;		// index == ChokePoint::index x ChokePoint::index
	const CPPath						m_EmptyPath;
	int									m_baseCount;

	unsigned m_areaMark = 0;
};


Area * mainArea(MapImpl * pMap, BWAPI::TilePosition topLeft, BWAPI::TilePosition size);


}} // namespace BWEM::detail


#endif

