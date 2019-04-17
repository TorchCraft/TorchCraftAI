//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_BASE_H
#define BWEM_BASE_H

#include <BWAPI.h>
#include "utils.h"
#include "neutral.h"
#include "defs.h"


namespace BWEM {

class Ressource;
class Mineral;
class Geyser;
class Area;
class Map;




//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Base
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// After Areas and ChokePoints, Bases are the third kind of object BWEM automatically computes from Brood War's maps.
// A Base is essentially a suggested location (intended to be optimal) to put a Command Center, Nexus, or Hatchery.
// It also provides information on the ressources available, and some statistics.
// A Base alway belongs to some Area. An Area may contain zero, one or several Bases.
// Like Areas and ChokePoints, the number and the addresses of Base instances remain unchanged.
//
// Bases inherit utils::UserData, which provides free-to-use data.

class Base : public utils::UserData
{
public:


	// Tells whether this Base's location is contained in Map::StartingLocations()
	// Note: all players start at locations taken from Map::StartingLocations(),
	//       which doesn't mean all the locations in Map::StartingLocations() are actually used.
	bool							Starting() const			{ return m_starting; }

	// Returns the Area this Base belongs to.
	const Area *					GetArea() const				{ return m_pArea; }

	// Returns the location of this Base (top left Tile position).
	// If Starting() == true, it is guaranteed that the loction corresponds exactly to one of Map::StartingLocations().
	const BWAPI::TilePosition &		Location() const			{ return m_location; }

	// Returns the location of this Base (center in pixels).
	const BWAPI::Position &			Center() const				{ return m_center; }

	// Returns the available Minerals.
	// These Minerals are assigned to this Base (it is guaranteed that no other Base provides them).
	// Note: The size of the returned list may decrease, as some of the Minerals may get destroyed.
	const std::vector<Mineral *> &	Minerals() const			{ return m_Minerals; }

	// Returns the available Geysers.
	// These Geysers are assigned to this Base (it is guaranteed that no other Base provides them).
	// Note: The size of the returned list may NOT decrease, as Geysers never get destroyed.
	const std::vector<Geyser *> &	Geysers() const				{ return m_Geysers; }

	// Returns the blocking Minerals.
	// These Minerals are special ones: they are placed at the exact location of this Base (or very close),
	// thus blocking the building of a Command Center, Nexus, or Hatchery.
	// So before trying to build this Base, one have to finish gathering these Minerals first.
	// Fortunately, these are guaranteed to have their InitialAmount() <= 8.
	// As an example of blocking Minerals, see the two islands in Andromeda.scx.
	// Note: if Starting() == true, an empty list is returned.
	// Note Base::BlockingMinerals() should not be confused with ChokePoint::BlockingNeutral() and Neutral::Blocking():
	//      the last two refer to a Neutral blocking a ChokePoint, not a Base.
	const std::vector<Mineral *> &	BlockingMinerals() const	{ return m_BlockingMinerals; }

	Base &							operator=(const Base &) = delete;

////////////////////////////////////////////////////////////////////////////
//	Details: The functions below are used by the BWEM's internals

									Base(Area * pArea, const BWAPI::TilePosition & location, const std::vector<Ressource *> & AssignedRessources, const std::vector<Mineral *> & BlockingMinerals);
									Base(const Base & Other);
	void							SetStartingLocation(const BWAPI::TilePosition & actualLocation);
	void							OnMineralDestroyed(const Mineral * pMineral);

private:
	Map *							GetMap() const				{ return m_pMap; }
	Map *							GetMap()					{ return m_pMap; }

	Map * const						m_pMap;
	Area * const					m_pArea;
	BWAPI::TilePosition				m_location;
	BWAPI::Position					m_center;
	std::vector<Mineral *>			m_Minerals;
	std::vector<Geyser *>			m_Geysers;
	std::vector<Mineral *>			m_BlockingMinerals;
	bool							m_starting = false;
};





} // namespace BWEM


#endif

