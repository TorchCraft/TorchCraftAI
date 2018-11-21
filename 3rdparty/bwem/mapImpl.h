//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_MAP_IMPL_H
#define BWEM_MAP_IMPL_H

#include <BWAPI.h>
#include "graph.h"
#include "map.h"
#include "tiles.h"
#include <queue>
#include <memory>
#include "utils.h"
#include "defs.h"


namespace BWEM {

class Neutral;
class Mineral;
class Geyser;
class StaticBuilding;
class ChokePoint;

namespace detail {

using namespace std;
using namespace utils;
using namespace BWAPI_ext;



class TempAreaInfo;
//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class MapImpl
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//

class MapImpl : public Map
{
	friend class Graph;

public:
	// Downcast helpers
	static MapImpl *			Get(Map * pMap)				{ return static_cast<MapImpl *>(pMap); }
	static const MapImpl *		Get(const Map * pMap)		{ return static_cast<const MapImpl *>(pMap); }

	using Map::GetTile_;
	using Map::GetMiniTile_;

								MapImpl();
								~MapImpl();

	void						Initialize(BWAPI::Game* bw) override;

	bool						AutomaticPathUpdate() const override					{ return m_automaticPathUpdate; }
	void						EnableAutomaticPathAnalysis() const override			{ m_automaticPathUpdate = true; }

	bool						FindBasesForStartingLocations() override;

	altitude_t					MaxAltitude() const override							{ return m_maxAltitude; }

	int							BaseCount() const override								{ return GetGraph().BaseCount(); }
	int							ChokePointCount() const override						{ return GetGraph().ChokePoints().size(); }

	const vector<BWAPI::TilePosition> &			StartingLocations() const override		{ return m_StartingLocations; }

	const vector<unique_ptr<Geyser>> &			Geysers() const override				{ return m_Geysers; }
	const vector<unique_ptr<Mineral>> &			Minerals() const override				{ return m_Minerals; }
	const vector<unique_ptr<StaticBuilding>> &	StaticBuildings() const override		{ return m_StaticBuildings; }

	Mineral *					GetMineral(BWAPI::Unit u) const override;
	Geyser *					GetGeyser(BWAPI::Unit u) const override;

	void						OnMineralDestroyed(BWAPI::Unit u) override;
	void						OnStaticBuildingDestroyed(BWAPI::Unit u) override;

	const vector<Area> &		Areas() const override									{ return GetGraph().Areas(); }

	// Returns an Area given its id. Range = 1..Size()
	const Area *				GetArea(Area::id id) const override						{ return m_Graph.GetArea(id); }
	Area *						GetArea(Area::id id)									{ return m_Graph.GetArea(id); }

	const Area *				GetArea(BWAPI::WalkPosition w) const override			{ return m_Graph.GetArea(w); }
	const Area *				GetArea(BWAPI::TilePosition t) const override			{ return m_Graph.GetArea(t); }

	Area *						GetArea(BWAPI::WalkPosition w)							{ return m_Graph.GetArea(w); }
	Area *						GetArea(BWAPI::TilePosition t)							{ return m_Graph.GetArea(t); }

	const Area *				GetNearestArea(BWAPI::WalkPosition w) const override	{ return m_Graph.GetNearestArea(w); }
	const Area *				GetNearestArea(BWAPI::TilePosition t) const override	{ return m_Graph.GetNearestArea(t); }

	Area *						GetNearestArea(BWAPI::WalkPosition w)					{ return m_Graph.GetNearestArea(w); }
	Area *						GetNearestArea(BWAPI::TilePosition t)					{ return m_Graph.GetNearestArea(t); }


	const CPPath &				GetPath(const BWAPI::Position & a, const BWAPI::Position & b, int * pLength = nullptr) const override { return m_Graph.GetPath(a, b, pLength); }

	const class Graph &			GetGraph() const										{ return m_Graph; }
	class Graph &				GetGraph()												{ return m_Graph; }


	const vector<pair<pair<Area::id, Area::id>, BWAPI::WalkPosition>> &		RawFrontier() const override		{ return m_RawFrontier; }



	void						OnMineralDestroyed(const Mineral * pMineral);
	void						OnBlockingNeutralDestroyed(const Neutral * pBlocking);

private:
	void						ReplaceAreaIds(BWAPI::WalkPosition p, Area::id newAreaId);

	void						InitializeNeutrals(BWAPI::Game* bw);
	void						LoadData(BWAPI::Game* bw);
	void						DecideSeasOrLakes();
	void						ComputeAltitude();
	void						ProcessBlockingNeutrals();
	void						ComputeAreas();
	vector<pair<BWAPI::WalkPosition, MiniTile *>>
								SortMiniTiles();
	vector<TempAreaInfo>		ComputeTempAreas(const vector<pair<BWAPI::WalkPosition, MiniTile *>> & MiniTilesByDescendingAltitude);
	void						CreateAreas(const vector<TempAreaInfo> & TempAreaList);
	void						SetAreaIdInTiles();
	void						SetAreaIdInTile(BWAPI::TilePosition t);
	void						SetAltitudeInTile(BWAPI::TilePosition t);


	altitude_t							m_maxAltitude;

	mutable bool						m_automaticPathUpdate = false;

	class Graph							m_Graph;
	vector<unique_ptr<Mineral>>			m_Minerals;
	vector<unique_ptr<Geyser>>			m_Geysers;
	vector<unique_ptr<StaticBuilding>>	m_StaticBuildings;
	vector<BWAPI::TilePosition>			m_StartingLocations;

	vector<pair<pair<Area::id, Area::id>, BWAPI::WalkPosition>>	m_RawFrontier;
};





}} // namespace BWEM::detail


#endif

