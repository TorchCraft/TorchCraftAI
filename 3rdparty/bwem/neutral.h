//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_NEUTRAL_H
#define BWEM_NEUTRAL_H

#include <BWAPI.h>
#include "bwapiExt.h"
#include <vector>
#include "utils.h"
#include "defs.h"


namespace BWEM {

class Ressource;
class Mineral;
class Geyser;
class Area;
class StaticBuilding;
class Map;

//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Neutral
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Neutral is the abstract base class for a small hierarchy of wrappers around some BWAPI::Units
// The units concerned are the Ressources (Minerals and Geysers) and the static Buildings.
// Stacked Neutrals are supported, provided they share the same type at the same location.
//

class Neutral : public utils::UserData
{
public:

	// If this Neutral is a Ressource, returns a typed pointer to this Ressource.
	// Otherwise, returns nullptr.
	virtual Ressource *				IsRessource()				{ return nullptr; }
	virtual const Ressource *		IsRessource() const			{ return nullptr; }

	// If this Neutral is a Mineral, returns a typed pointer to this Mineral.
	// Otherwise, returns nullptr.
	virtual Mineral *				IsMineral()					{ return nullptr; }
	virtual const Mineral *			IsMineral() const			{ return nullptr; }

	// If this Neutral is a Geyser, returns a typed pointer to this Geyser.
	// Otherwise, returns nullptr.
	virtual Geyser *				IsGeyser()					{ return nullptr; }
	virtual const Geyser *			IsGeyser() const			{ return nullptr; }

	// If this Neutral is a StaticBuilding, returns a typed pointer to this StaticBuilding.
	// Otherwise, returns nullptr.
	virtual StaticBuilding *		IsStaticBuilding()			{ return nullptr; }
	virtual const StaticBuilding *	IsStaticBuilding() const	{ return nullptr; }

	// Returns the BWAPI::Unit this Neutral is wrapping around.
	BWAPI::Unit						Unit() const				{ return m_bwapiUnit; }

	// Returns the BWAPI::UnitType of the BWAPI::Unit this Neutral is wrapping around.
	BWAPI::UnitType					Type() const				{ return m_bwapiType; }

	// Returns the center of this Neutral, in pixels (same as Unit()->getInitialPosition()).
	BWAPI::Position					Pos() const					{ return m_pos; }

	// Returns the top left Tile position of this Neutral (same as Unit()->getInitialTilePosition()).
	BWAPI::TilePosition				TopLeft() const				{ return m_topLeft; }

	// Returns the bottom right Tile position of this Neutral
	BWAPI::TilePosition				BottomRight() const;

	// Returns the size of this Neutral, in Tiles (same as Type()->tileSize())
	BWAPI::TilePosition				Size() const				{ return m_size; }

	// Tells whether this Neutral is blocking some ChokePoint.
	// This applies to Minerals and StaticBuildings only.
	// For each blocking Neutral, a pseudo ChokePoint (which is Blocked()) is created on top of it,
	// with the exception of stacked blocking Neutrals for which only one pseudo ChokePoint is created.
	// Cf. definition of pseudo ChokePoints in class ChokePoint comment.
	// Cf. ChokePoint::BlockingNeutral and ChokePoint::Blocked.
	bool							Blocking() const			{ return !m_blockedAreas.empty(); }

	// If Blocking() == true, returns the set of Areas blocked by this Neutral.
	std::vector<const Area *>		BlockedAreas() const;

	// Returns the next Neutral stacked over this Neutral, if ever.
	// To iterate through the whole stack, one can use the following:
	// for (const Neutral * n = Map::GetTile(TopLeft()).GetNeutral() ; n ; n = n->NextStacked())
	Neutral *						NextStacked() const			{ return m_pNextStacked; }

	// Returns the last Neutral stacked over this Neutral, if ever.
	Neutral *						LastStacked()				{ Neutral * pTop = this; while (pTop->m_pNextStacked) pTop = pTop->m_pNextStacked; return pTop; }

	Neutral &						operator=(const Neutral &) = delete;

	////////////////////////////////////////////////////////////////////////////
//	Details: The functions below are used by the BWEM's internals

	void							SetBlocking(const std::vector<BWAPI::WalkPosition> & blockedAreas);

protected:
									Neutral(BWAPI::Unit u, Map * pMap);
									~Neutral();
	Map * 					GetMap() const				{ return m_pMap; }

private:
	void							PutOnTiles();
	void							RemoveFromTiles();

	const BWAPI::Unit				m_bwapiUnit;
	const BWAPI::UnitType			m_bwapiType;
	const BWAPI::Position			m_pos;
	BWAPI::TilePosition				m_topLeft;
	const BWAPI::TilePosition		m_size;
	Map * const 					m_pMap;
	Neutral *						m_pNextStacked = nullptr;
	std::vector<BWAPI::WalkPosition>m_blockedAreas;
};



//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Ressource
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// A Ressource is either a Mineral or a Geyser

class Ressource : public Neutral
{
public:
							Ressource(BWAPI::Unit u, Map * pMap);

	Ressource *				IsRessource() override		{ return this; }
	const Ressource *		IsRessource() const override{ return this; }

	// Returns the initial amount of ressources for this Ressource (same as Unit()->getInitialResources).
	int						InitialAmount() const		{ return m_initialAmount; }

	// Returns the current amount of ressources for this Ressource (same as Unit()->getResources).
	int						Amount() const				{ return Unit()->getResources(); }

private:
	const int				m_initialAmount;
};



//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Mineral
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Minerals Correspond to the units in BWAPI::getStaticNeutralUnits() for which getType().isMineralField(),

class Mineral : public Ressource
{
public:
							Mineral(BWAPI::Unit u, Map * pMap);
							~Mineral();

	Mineral *				IsMineral() override		{ return this; }
	const Mineral *			IsMineral() const override	{ return this; }

private:
};



//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Geyser
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Geysers Correspond to the units in BWAPI::getStaticNeutralUnits() for which getType() == Resource_Vespene_Geyser

class Geyser : public Ressource
{
public:
							Geyser(BWAPI::Unit u, Map * pMap);
							~Geyser();

	Geyser *				IsGeyser() override			{ return this; }
	const Geyser *			IsGeyser() const override	{ return this; }

private:
};



//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class StaticBuilding
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// StaticBuildings Correspond to the units in BWAPI::getStaticNeutralUnits() which are:
// - Neutral
// - Not flying
// - Buildings
//
// StaticBuilding also wrappers some special units like Special_Pit_Door.
//
// An absent feature here is handling other neutral non-flying units that can't move, like neutral Zerg_Eggs

class StaticBuilding : public Neutral
{
public:
							StaticBuilding(BWAPI::Unit u, Map * pMap);

	StaticBuilding *		IsStaticBuilding() override			{ return this; }
	const StaticBuilding *	IsStaticBuilding() const override	{ return this; }

private:
};




} // namespace BWEM


#endif

