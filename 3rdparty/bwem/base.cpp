//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#include "base.h"
#include "graph.h"
#include "mapImpl.h"
#include "neutral.h"
#include "bwapiExt.h"


using namespace BWAPI;
using namespace BWAPI::UnitTypes::Enum;

using namespace std;


namespace BWEM {

using namespace detail;
using namespace BWAPI_ext;


//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Base
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////


Base::Base(Area * pArea, const TilePosition & location, const vector<Ressource *> & AssignedRessources, const vector<Mineral *> & BlockingMinerals)
	: m_pArea(pArea),
	m_pMap(pArea->GetMap()),
	m_location(location),
	m_center(Position(location) + Position(UnitType(Terran_Command_Center).tileSize()) / 2),
	m_BlockingMinerals(BlockingMinerals)
{
	bwem_assert(!AssignedRessources.empty());

	for (Ressource * r : AssignedRessources)
		if		(Mineral * m = r->IsMineral())	m_Minerals.push_back(m);
		else if (Geyser * g = r->IsGeyser())		m_Geysers.push_back(g);
}


Base::Base(const Base & Other)
	: m_pMap(Other.m_pMap), m_pArea(Other.m_pArea)
{
	bwem_assert(false);
}


void Base::SetStartingLocation(const TilePosition & actualLocation)
{
	m_starting = true;
	m_location = actualLocation;
	m_center = Position(actualLocation) + Position(UnitType(Terran_Command_Center).tileSize()) / 2;
}


void Base::OnMineralDestroyed(const Mineral * pMineral)
{
	bwem_assert(pMineral);

	auto iMineral = find(m_Minerals.begin(), m_Minerals.end(), pMineral);
	if (iMineral != m_Minerals.end())
		fast_erase(m_Minerals, distance(m_Minerals.begin(), iMineral));

	iMineral = find(m_BlockingMinerals.begin(), m_BlockingMinerals.end(), pMineral);
	if (iMineral != m_BlockingMinerals.end())
		fast_erase(m_BlockingMinerals, distance(m_BlockingMinerals.begin(), iMineral));
}

	
} // namespace BWEM



