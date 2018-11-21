//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_AREA_H
#define BWEM_AREA_H

#include <BWAPI.h>
#include "bwapiExt.h"
#include "base.h"
#include "utils.h"
#include "defs.h"
#include <map>

namespace BWEM {

class Mineral;
class Geyser;
class Tile;
class Base;
class ChokePoint;
class Map;

namespace detail { class Graph; }



//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Area
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Areas are regions that BWEM automatically computes from Brood War's maps
// Areas aim at capturing relevant regions that can be walked, though they may contain small inner non walkable regions called lakes.
// More formally:
//  - An area consists in a set of 4-connected MiniTiles, which are either Terrain-MiniTiles or Lake-MiniTiles.
//  - An Area is delimited by the side of the Map, by Water-MiniTiles, or by other Areas. In the latter case
//    the adjoining Areas are called neighbouring Areas, and each pair of such Areas defines at least one ChokePoint.
// Like ChokePoints and Bases, the number and the addresses of Area instances remain unchanged.
// To access Areas one can use their ids or their addresses with equivalent efficiency.
//
// Areas inherit utils::Markable, which provides marking ability
// Areas inherit utils::UserData, which provides free-to-use data.

class Area : public utils::Markable<Area, int>, public utils::UserData
{
public:
	typedef int16_t					id;

	typedef int16_t					groupId;

	// Unique id > 0 of this Area. Range = 1 .. Map::Areas().size()
	// this == Map::GetArea(Id())
	// Id() == Map::GetMiniTile(w).AreaId() for each walkable MiniTile w in this Area.
	// Area::ids are guaranteed to remain unchanged.
	id								Id() const						{ return m_id; }

	// Unique id > 0 of the group of Areas which are accessible from this Area.
	// For each pair (a, b) of Areas: a->GroupId() == b->GroupId()  <==>  a->AccessibleFrom(b)
	// A groupId uniquely identifies a maximum set of mutually accessible Areas, that is, in the absence of blocking ChokePoints, a continent.
	groupId							GroupId() const					{ return m_groupId; }

	// Bounding box of this Area.
	const BWAPI::TilePosition &		TopLeft() const					{ return m_topLeft ; }
	const BWAPI::TilePosition &		BottomRight() const				{ return m_bottomRight ; }
	BWAPI::TilePosition 					BoundingBoxSize() const;

	// Position of the MiniTile with the highest Altitude() value.
	const BWAPI::WalkPosition &		Top() const						{ return m_top; }

	// Returns Map::GetMiniTile(Top()).Altitude().
	altitude_t						MaxAltitude() const				{ return m_maxAltitude; }

	// Returns the number of MiniTiles in this Area.
	// This most accurately defines the size of this Area.
	int								MiniTiles() const				{ return m_miniTiles; }

	// Returns the percentage of low ground Tiles in this Area.
	int								LowGroundPercentage() const			{ return (m_tiles - m_highGroundTiles - m_veryHighGroundTiles) * 100 / m_tiles; }

	// Returns the percentage of high ground Tiles in this Area.
	int								HighGroundPercentage() const		{ return m_highGroundTiles * 100 / m_tiles; }

	// Returns the percentage of very high ground Tiles in this Area.
	int								VeryHighGroundPercentage() const	{ return m_veryHighGroundTiles * 100 / m_tiles; }

	// Returns the ChokePoints between this Area and the neighbouring ones.
	// Note: if there are no neighbouring Areas, then an empty set is returned.
	// Note there may be more ChokePoints returned than the number of neighbouring Areas, as there may be several ChokePoints between two Areas (Cf. ChokePoints(const Area * pArea)).
	const std::vector<const ChokePoint *> &	ChokePoints() const		{ return m_ChokePoints; }

	// Returns the ChokePoints between this Area and pArea.
	// Assumes pArea is a neighbour of this Area, i.e. ChokePointsByArea().find(pArea) != ChokePointsByArea().end()
	// Note: there is always at least one ChokePoint between two neighbouring Areas.
	const std::vector<ChokePoint> &	ChokePoints(const Area * pArea) const;

	// Returns the ChokePoints of this Area grouped by neighbouring Areas
	// Note: if there are no neighbouring Areas, than an empty set is returned.
	const std::map<const Area *, const std::vector<ChokePoint> *> &	ChokePointsByArea() const	{ return m_ChokePointsByArea; }

	// Returns the accessible neighbouring Areas.
	// The accessible neighbouring Areas are a subset of the neighbouring Areas (the neighbouring Areas can be iterated using ChokePointsByArea()).
	// Two neighbouring Areas are accessible from each over if at least one the ChokePoints they share is not Blocked (Cf. ChokePoint::Blocked).
	const std::vector<const Area *>&AccessibleNeighbours() const	{ return m_AccessibleNeighbours; }

	// Returns whether this Area is accessible from pArea, that is, if they share the same GroupId().
	// Note: accessibility is always symmetrical.
	// Note: even if a and b are neighbouring Areas,
	//       we can have: a->AccessibleFrom(b)
	//       and not:     contains(a->AccessibleNeighbours(), b)
	// See also GroupId()
	bool							AccessibleFrom(const Area * pArea) const	{ return GroupId() == pArea->GroupId(); }

	// Returns the Minerals contained in this Area.
	// Note: only a call to Map::OnMineralDestroyed(BWAPI::Unit u) may change the result (by removing eventually one element).
	const std::vector<Mineral *> &	Minerals() const		{ return m_Minerals; }

	// Returns the Geysers contained in this Area.
	// Note: the result will remain unchanged.
	const std::vector<Geyser *> &	Geysers() const			{ return m_Geysers; }

	// Returns the Bases contained in this Area.
	// Note: the result will remain unchanged.
	const std::vector<Base> &		Bases() const			{ return m_Bases; }

	Map *							GetMap() const;

	Area &							operator=(const Area &) = delete;

////////////////////////////////////////////////////////////////////////////
//	Details: The functions below are used by the BWEM's internals

									Area(detail::Graph * pGraph, id areaId, BWAPI::WalkPosition top, int miniTiles);
									Area(const Area & Other);
	void							AddChokePoints(Area * pArea, std::vector<ChokePoint> * pChokePoints);
	void							AddMineral(Mineral * pMineral);
	void							AddGeyser(Geyser * pGeyser);
	void							AddTileInformation(const BWAPI::TilePosition t, const Tile & tile);
	void							OnMineralDestroyed(const Mineral * pMineral);
	void							PostCollectInformation();
	std::vector<int>				ComputeDistances(const ChokePoint * pStartCP, const std::vector<const ChokePoint *> & TargetCPs) const;
	void							UpdateAccessibleNeighbours();
	void							SetGroupId(groupId gid)	{ bwem_assert(gid >= 1); m_groupId = gid; }
	void							CreateBases();
	std::vector<Base> &				Bases()					{ return m_Bases; }

private:
	const detail::Graph *			GetGraph() const		{ return m_pGraph; }
	detail::Graph *					GetGraph()				{ return m_pGraph; }

	int								ComputeBaseLocationScore(BWAPI::TilePosition location) const;
	bool							ValidateBaseLocation(BWAPI::TilePosition location, std::vector<Mineral *> & BlockingMinerals) const;
	std::vector<int>				ComputeDistances(BWAPI::TilePosition start, const std::vector<BWAPI::TilePosition> & Targets) const;

	detail::Graph * const			m_pGraph;
	id								m_id;
	groupId							m_groupId = 0;
	BWAPI::WalkPosition				m_top;
	BWAPI::TilePosition				m_topLeft     = {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
	BWAPI::TilePosition				m_bottomRight = {std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
	altitude_t						m_maxAltitude;
	int								m_miniTiles;
	int								m_tiles = 0;
	int								m_buildableTiles = 0;
	int								m_highGroundTiles = 0;
	int								m_veryHighGroundTiles = 0;

	std::map<const Area *, const std::vector<ChokePoint> *>	m_ChokePointsByArea;
	std::vector<const Area *>		m_AccessibleNeighbours;
	std::vector<const ChokePoint *>	m_ChokePoints;
	std::vector<Mineral *>			m_Minerals;
	std::vector<Geyser *>			m_Geysers;
	std::vector<Base>				m_Bases;
};





} // namespace BWEM


#endif

