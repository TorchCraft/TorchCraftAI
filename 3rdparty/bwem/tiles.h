//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_TILES_H
#define BWEM_TILES_H

#include <BWAPI.h>
#include "area.h"
#include "utils.h"
#include "defs.h"


namespace BWEM {

class Neutral;


//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class MiniTile
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Corresponds to BWAPI/Starcraft's concept of minitile (8x8 pixels).
// MiniTiles are accessed using WalkPositions (Cf. Map::GetMiniTile).
// A Map holds Map::WalkSize().x * Map::WalkSize().y MiniTiles as its "MiniTile map".
// A MiniTile contains essentialy 3 informations:
//	- its Walkability
//	- its altitude (distance from the nearest non walkable MiniTile, except those which are part of small enough zones (lakes))
//	- the id of the Area it is part of, if ever.
// The whole process of analysis of a Map relies on the walkability information
// from which are derived successively : altitudes, Areas, ChokePoints.

class MiniTile
{
public:
	// Corresponds approximatively to BWAPI::isWalkable
	// The differences are:
	//  - For each BWAPI's unwalkable MiniTile, we also mark its 8 neighbours as not walkable.
	//    According to some tests, this prevents from wrongly pretending one small unit can go by some thin path.
	//  - The relation buildable ==> walkable is enforced, by marking as walkable any MiniTile part of a buildable Tile (Cf. Tile::Buildable)
	// Among the MiniTiles having Altitude() > 0, the walkable ones are considered Terrain-MiniTiles, and the other ones Lake-MiniTiles.
	bool				Walkable() const			{ return m_areaId != 0; }

	// Distance in pixels between the center of this MiniTile and the center of the nearest Sea-MiniTile
	// Sea-MiniTiles all have their Altitude() equal to 0.
	// MiniTiles having Altitude() > 0 are not Sea-MiniTiles. They can be either Terrain-MiniTiles or Lake-MiniTiles.
	altitude_t			Altitude() const			{ return m_altitude; }

	// Sea-MiniTiles are unwalkable MiniTiles that have their Altitude() equal to 0.
	bool				Sea() const					{ return m_altitude == 0; }

	// Lake-MiniTiles are unwalkable MiniTiles that have their Altitude() > 0.
	// They form small zones (inside Terrain-zones) that can be eaysily walked around (e.g. Starcraft's doodads)
	// The intent is to preserve the continuity of altitudes inside Areas.
	bool				Lake() const				{ return (m_altitude != 0) && !Walkable(); }

	// Terrain MiniTiles are just walkable MiniTiles
	bool				Terrain() const				{ return Walkable(); }

	// For Sea and Lake MiniTiles, returns 0
	// For Terrain MiniTiles, returns a non zero id:
	//    - if (id > 0), id uniquely identifies the Area A that contains this MiniTile.
	//      Moreover we have: A.Id() == id and Map::GetArea(id) == A
	//      For more information about positive Area::ids, see Area::Id()
	//    - if (id < 0), then this MiniTile is part of a Terrain-zone that was considered too small to create an Area for it.
	//      Note: negative Area::ids start from -2
	// Note: because of the lakes, Map::GetNearestArea should be prefered over Map::GetArea.
	Area::id			AreaId() const				{ return m_areaId; }


////////////////////////////////////////////////////////////////////////////
//	Details: The functions below are used by the BWEM's internals

	void				SetWalkable(bool walkable)	{ m_areaId = (walkable ? -1 : 0); m_altitude = (walkable ? -1 : 1); }
	bool				SeaOrLake() const			{ return m_altitude == 1; }
	void				SetSea()					{ bwem_assert(!Walkable() && SeaOrLake()); m_altitude = 0; }
	void				SetLake()					{ bwem_assert(!Walkable() && Sea()); m_altitude = -1; }
	bool				AltitudeMissing() const		{ return m_altitude == -1; }
	void				SetAltitude(altitude_t a)	{ bwem_assert_debug_only(AltitudeMissing() && (a > 0)); m_altitude = a; }
	bool				AreaIdMissing() const		{ return m_areaId == -1; }
	void				SetAreaId(Area::id id)		{ bwem_assert(AreaIdMissing() && (id >= 1)); m_areaId = id; }
	void				ReplaceAreaId(Area::id id)	{ bwem_assert((m_areaId > 0) && ((id >= 1) || (id <= -2)) && (id != m_areaId)); m_areaId = id; }
	void				SetBlocked()				{ bwem_assert(AreaIdMissing()); m_areaId = blockingCP; }
	bool				Blocked() const				{ return m_areaId == blockingCP; }
	void				ReplaceBlockedAreaId(Area::id id)	{ bwem_assert((m_areaId == blockingCP) && (id >= 1)); m_areaId = id; }

private:
	altitude_t			m_altitude = -1;			// 0 for seas  ;  != 0 for terrain and lakes (-1 = not computed yet)  ;  1 = SeaOrLake intermediate value
	Area::id			m_areaId = -1;			// 0 -> unwalkable  ;  > 0 -> index of some Area  ;  < 0 -> some walkable terrain, but too small to be part of an Area
	static const Area::id blockingCP;
};



//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Tile
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Corresponds to BWAPI/Starcraft's concept of tile (32x32 pixels).
// Tiles are accessed using TilePositions (Cf. Map::GetTile).
// A Map holds Map::Size().x * Map::Size().y Tiles as its "Tile map".
//
// It should be noted that a Tile exactly overlaps 4 x 4 MiniTiles.
// As there are 16 times as many MiniTiles as Tiles, we allow a Tiles to contain more data than MiniTiles.
// As a consequence, Tiles should be preferred over MiniTiles, for efficiency.
// The use of Tiles is further facilitated by some functions like Tile::AreaId or Tile::MinAltitude
// which somewhat aggregate the MiniTile's corresponding information
//
// Tiles inherit utils::Markable, which provides marking ability
// Tiles inherit utils::UserData, which provides free-to-use data.

class Tile : public utils::Markable<Tile, int>, public utils::UserData
{
public:
	// Corresponds to BWAPI::isBuildable
	// Note: BWEM enforces the relation buildable ==> walkable (Cf. MiniTile::Walkable)
	bool				Buildable() const				{ return m_bits.buildable; }

	// Tile::AreaId() somewhat aggregates the MiniTile::AreaId() values of the 4 x 4 sub-MiniTiles.
	// Let S be the set of MiniTile::AreaId() values for each walkable MiniTile in this Tile.
	// If empty(S), returns 0. Note: in this case, no contained MiniTile is walkable, so all of them have their AreaId() == 0.
	// If S = {a}, returns a (whether positive or negative).
	// If size(S) > 1 returns -1 (note that -1 is never returned by MiniTile::AreaId()).
	Area::id			AreaId() const					{ return m_areaId; }

	// Tile::MinAltitude() somewhat aggregates the MiniTile::Altitude() values of the 4 x 4 sub-MiniTiles.
	// Returns the minimum value.
	altitude_t			MinAltitude() const				{ return m_minAltitude; }

	// Tells if at least one of the sub-MiniTiles is Walkable.
	bool				Walkable() const				{ return m_areaId != 0; }

	// Tells if at least one of the sub-MiniTiles is a Terrain-MiniTile.
	bool				Terrain() const					{ return Walkable(); }

	// 0: lower ground    1: high ground    2: very high ground
	// Corresponds to BWAPI::getGroundHeight / 2
	int					GroundHeight() const			{ return m_bits.groundHeight; }

	// Tells if this Tile is part of a doodad.
	// Corresponds to BWAPI::getGroundHeight % 2
	bool				Doodad() const					{ return m_bits.doodad; }

	// If any Neutral occupies this Tile, returns it (note that all the Tiles it occupies will then return it).
	// Otherwise, returns nullptr.
	// Neutrals are Minerals, Geysers and StaticBuildings (Cf. Neutral).
	// In some maps (e.g. Benzene.scx), several Neutrals are stacked at the same location.
	// In this case, only the "bottom" one is returned, while the other ones can be accessed using Neutral::NextStacked().
	// Because Neutrals never move on the Map, the returned value is guaranteed to remain the same, unless some Neutral
	// is destroyed and BWEM is informed of that by a call of Map::OnMineralDestroyed(BWAPI::Unit u) for exemple. In such a case,
	// BWEM automatically updates the data by deleting the Neutral instance and clearing any reference to it such as the one
	// returned by Tile::GetNeutral(). In case of stacked Neutrals, the next one is then returned.
	Neutral *			GetNeutral() const				{ return m_pNeutral; }

	// Returns the number of Neutrals that occupy this Tile (Cf. GetNeutral).
	int					StackedNeutrals() const;

////////////////////////////////////////////////////////////////////////////
//	Details: The functions below are used by the BWEM's internals

	void				SetBuildable()					{ m_bits.buildable = 1; }
	void				SetGroundHeight(int h)			{ bwem_assert((0 <= h) && (h <= 2)); m_bits.groundHeight = h; }
	void				SetDoodad()						{ m_bits.doodad = 1; }
	void				AddNeutral(Neutral * pNeutral)	{ bwem_assert(!m_pNeutral && pNeutral); m_pNeutral = pNeutral; }
	void				SetAreaId(Area::id id)			{ bwem_assert((id == -1) || (!m_areaId && id)); m_areaId = id; }
	void				ResetAreaId()					{ m_areaId = 0; }
	void				SetMinAltitude(altitude_t a)	{ bwem_assert(a >= 0); m_minAltitude = a; }
	void				RemoveNeutral(Neutral * pNeutral){ bwem_assert(pNeutral && (m_pNeutral == pNeutral)); utils::unused(pNeutral); m_pNeutral = nullptr; }
	int					InternalData() const			{ return m_internalData; }
	void				SetInternalData(int data) const	{ m_internalData = data; }

						
private:
	struct Bits
	{
						Bits() : buildable(0), groundHeight(0), doodad(0) {}
		uint8_t			buildable:1;
		uint8_t			groundHeight:2;
		uint8_t			doodad:1;
	};
	
	Neutral *			m_pNeutral = nullptr;
	altitude_t			m_minAltitude;
	Area::id			m_areaId = 0;
	mutable int			m_internalData = 0;
	Bits				m_bits;
};


namespace utils {


// Helpers for generic use of Tiles and MiniTiles (see Map::BreadthFirstSearch for a use case)

template<class TPosition>
struct TileOfPosition;

template<>
struct TileOfPosition<BWAPI::TilePosition>
{
	typedef Tile type;
};

template<>
struct TileOfPosition<BWAPI::WalkPosition>
{
	typedef MiniTile type;
};



template<class TTile>
struct PositionOfTile;

template<>
struct PositionOfTile<Tile>
{
	typedef BWAPI::TilePosition type;
};

template<>
struct PositionOfTile<MiniTile>
{
	typedef BWAPI::WalkPosition type;
};




} // namespace utils




} // namespace BWEM


#endif

