//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#include "cp.h"
#include "mapImpl.h"
#include "neutral.h"


using namespace BWAPI;
using namespace BWAPI::UnitTypes::Enum;

using namespace std;


namespace BWEM {

using namespace detail;
using namespace BWAPI_ext;



//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class ChokePoint
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////


ChokePoint::ChokePoint(detail::Graph * pGraph, index idx, const Area * area1, const Area * area2, const deque<WalkPosition> & Geometry, Neutral * pBlockingNeutral)
: m_pGraph(pGraph), m_index(idx), m_Areas(area1, area2), m_Geometry(Geometry),
	m_pBlockingNeutral(pBlockingNeutral), m_blocked(pBlockingNeutral != nullptr), m_pseudo(pBlockingNeutral != nullptr)
{
	bwem_assert(!Geometry.empty());

	// Ensures that in the case where several neutrals are stacked, m_pBlockingNeutral points to the bottom one: 
	if (m_pBlockingNeutral) m_pBlockingNeutral = GetMap()->GetTile(m_pBlockingNeutral->TopLeft()).GetNeutral();

	m_nodes[end1] = Geometry.front();
	m_nodes[end2] = Geometry.back();

	int i = Geometry.size() / 2;
	while ((i > 0)                      && (GetMap()->GetMiniTile(Geometry[i-1]).Altitude() > GetMap()->GetMiniTile(Geometry[i]).Altitude())) --i;
	while ((i < (int)Geometry.size()-1) && (GetMap()->GetMiniTile(Geometry[i+1]).Altitude() > GetMap()->GetMiniTile(Geometry[i]).Altitude())) ++i;
	m_nodes[middle] = Geometry[i];

	for (int n = 0 ; n < node_count ; ++n)
		for (const Area * pArea : {area1, area2})
		{
			WalkPosition & nodeInArea = (pArea == m_Areas.first) ? m_nodesInArea[n].first : m_nodesInArea[n].second;
			nodeInArea = GetMap()->BreadthFirstSearch(m_nodes[n],
				[pArea, this](const MiniTile & miniTile, WalkPosition w)	// findCond
					{ return (miniTile.AreaId() == pArea->Id()) && !GetMap()->GetTile(TilePosition(w), check_t::no_check).GetNeutral(); },
				[pArea, this](const MiniTile & miniTile, WalkPosition)		// visitCond
					{ return (miniTile.AreaId() == pArea->Id()) || (Blocked() && (miniTile.Blocked())); }
				);
		}
}


ChokePoint::ChokePoint(const ChokePoint & Other)
	: m_pGraph(Other.m_pGraph), m_index(0), m_pseudo(false)
{
	bwem_assert(false);
}


Map * ChokePoint::GetMap() const
{
	return m_pGraph->GetMap();
}


const BWAPI::WalkPosition & ChokePoint::PosInArea(node n, const Area * pArea) const
{
	bwem_assert((pArea == m_Areas.first) || (pArea == m_Areas.second));
	return (pArea == m_Areas.first) ? m_nodesInArea[n].first : m_nodesInArea[n].second;
}


int ChokePoint::DistanceFrom(const ChokePoint * cp) const
{
	return GetGraph()->Distance(this, cp);
}


const CPPath & ChokePoint::GetPathTo(const ChokePoint * cp) const
{
	return GetGraph()->GetPath(this, cp);
}


// Assumes pBlocking->RemoveFromTiles() has been called
void ChokePoint::OnBlockingNeutralDestroyed(const Neutral * pBlocking)
{
	bwem_assert(pBlocking && pBlocking->Blocking());

	if (m_pBlockingNeutral == pBlocking)
	{
		// Ensures that in the case where several neutrals are stacked, m_pBlockingNeutral points to the bottom one: 
		m_pBlockingNeutral = GetMap()->GetTile(m_pBlockingNeutral->TopLeft()).GetNeutral();

		if (!m_pBlockingNeutral)
			if (GetGraph()->GetMap()->AutomaticPathUpdate())
				m_blocked = false;
	}

}


} // namespace BWEM



