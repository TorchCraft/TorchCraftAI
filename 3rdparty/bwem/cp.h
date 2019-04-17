//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_CP_H
#define BWEM_CP_H

#include <BWAPI.h>
#include "bwapiExt.h"
#include <deque>
#include "utils.h"
#include "defs.h"


namespace BWEM {

class Neutral;
class Area;
class Map;

namespace detail { class Graph; }





//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class ChokePoint
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// ChokePoints are frontiers that BWEM automatically computes from Brood War's maps
// A ChokePoint represents (part of) the frontier between exactly 2 Areas. It has a form of line.
// A ChokePoint doesn't contain any MiniTile: All the MiniTiles whose positions are returned by its Geometry()
// are just guaranteed to be part of one of the 2 Areas.
// Among the MiniTiles of its Geometry, 3 particular ones called nodes can also be accessed using Pos(middle), Pos(end1) and Pos(end2).
// ChokePoints play an important role in BWEM:
//   - they define accessibility between Areas.
//   - the Paths provided by Map::GetPath are made of ChokePoints.
// Like Areas and Bases, the number and the addresses of ChokePoint instances remain unchanged.
//
// Pseudo ChokePoints:
// Some Neutrals can be detected as blocking Neutrals (Cf. Neutral::Blocking).
// Because only ChokePoints can serve as frontiers between Areas, BWEM automatically creates a ChokePoint
// for each blocking Neutral (only one in the case of stacked blocking Neutral).
// Such ChokePoints are called pseudo ChokePoints and they behave differently in several ways.
//
// ChokePoints inherit utils::Markable, which provides marking ability
// ChokePoints inherit utils::UserData, which provides free-to-use data.

class ChokePoint : public utils::Markable<ChokePoint, int>, public utils::UserData
{
public:
	// ChokePoint::middle denotes the "middle" MiniTile of Geometry(), while
	// ChokePoint::end1 and ChokePoint::end2 denote its "ends".
	// It is guaranteed that, among all the MiniTiles of Geometry(), ChokePoint::middle has the highest altitude value (Cf. MiniTile::Altitude()).
	enum node {end1, middle, end2, node_count};

	// Type of all the Paths used in BWEM (Cf. Map::GetPath).
	// See also the typedef CPPath.
	typedef std::vector<const ChokePoint *> Path;

	// Tells whether this ChokePoint is a pseudo ChokePoint, i.e., it was created on top of a blocking Neutral.
	bool									IsPseudo() const		{ return m_pseudo; }

	// Returns the two Areas of this ChokePoint.
	const std::pair<const Area *, const Area *> & GetAreas() const	{ return m_Areas; }

	// Returns the center of this ChokePoint.
	const BWAPI::WalkPosition &				Center() const			{ return Pos(middle); }

	// Returns the position of one of the 3 nodes of this ChokePoint (Cf. node definition).
	// Note: the returned value is contained in Geometry()
	const BWAPI::WalkPosition &				Pos(node n) const		{ bwem_assert(n < node_count); return m_nodes[n]; }

	// Pretty much the same as Pos(n), except that the returned MiniTile position is guaranteed to be part of pArea.
	// That is: Map::GetArea(PosInArea(n, pArea)) == pArea.
	const BWAPI::WalkPosition &				PosInArea(node n, const Area * pArea) const;

	// Returns the set of positions that defines the shape of this ChokePoint.
	// Note: none of these MiniTiles actually belongs to this ChokePoint (a ChokePoint doesn't contain any MiniTile).
	//       They are however guaranteed to be part of one of the 2 Areas.
	// Note: the returned set contains Pos(middle), Pos(end1) and Pos(end2).
	// If IsPseudo(), returns {p} where p is the position of a walkable MiniTile near from BlockingNeutral()->Pos().
	const std::deque<BWAPI::WalkPosition> &	Geometry() const		{ return m_Geometry; }

	// If !IsPseudo(), returns false.
	// Otherwise, returns whether this ChokePoint is considered blocked.
	// Normally, a pseudo ChokePoint either remains blocked, or switches to not blocked when BlockingNeutral()
	// is destroyed and there is no remaining Neutral stacked with it.
	// However, in the case where Map::AutomaticPathUpdate() == false, Blocked() will always return true
	// whatever BlockingNeutral() returns.
	// Cf. Area::AccessibleNeighbours().
	bool									Blocked() const			{ return m_blocked; }

	// If !IsPseudo(), returns nullptr.
	// Otherwise, returns a pointer to the blocking Neutral on top of which this pseudo ChokePoint was created,
	// unless this blocking Neutral has been destroyed.
	// In this case, returns a pointer to the next blocking Neutral that was stacked at the same location,
	// or nullptr if no such Neutral exists.
	Neutral *								BlockingNeutral() const	{ return m_pBlockingNeutral; }

	// If AccessibleFrom(cp) == false, returns -1.
	// Otherwise, returns the ground distance in pixels between Center() and cp->Center().
	// Note: if this == cp, returns 0.
	// Time complexity: O(1)
	// Note: Corresponds to the length in pixels of GetPathTo(cp). So it suffers from the same lack of accuracy.
	//       In particular, the value returned tends to be slightly higher than expected when GetPathTo(cp).size() is high.
	int										DistanceFrom(const ChokePoint * cp) const;

	// Returns whether this ChokePoint is accessible from cp (through a walkable path).
	// Note: the relation is symmetric: this->AccessibleFrom(cp) == cp->AccessibleFrom(this)
	// Note: if this == cp, returns true.
	// Time complexity: O(1)
	bool									AccessibleFrom(const ChokePoint * cp) const	{ return DistanceFrom(cp) >= 0; }

	// Returns a list of ChokePoints, which is intended to be the shortest walking path from this ChokePoint to cp.
	// The path always starts with this ChokePoint and ends with cp, unless AccessibleFrom(cp) == false.
	// In this case, an empty list is returned.
	// Note: if this == cp, returns [cp].
	// Time complexity: O(1)
	// To get the length of the path returned in pixels, use DistanceFrom(cp).
	// Note: all the possible Paths are precomputed during Map::Initialize().
	//       The best one is then stored for each pair of ChokePoints.
	//       However, only the center of the ChokePoints is considered.
	//       As a consequence, the returned path may not be the shortest one.
	const ChokePoint::Path &				GetPathTo(const ChokePoint * cp) const;

	Map *									GetMap() const;

	ChokePoint &							operator=(const ChokePoint &) = delete;

////////////////////////////////////////////////////////////////////////////
//	Details: The functions below are used by the BWEM's internals

	typedef int								index;
											ChokePoint(detail::Graph * pGraph, index idx, const Area * area1, const Area * area2, const std::deque<BWAPI::WalkPosition> & Geometry, Neutral * pBlockingNeutral = nullptr);
											ChokePoint(const ChokePoint & Other);
	void									OnBlockingNeutralDestroyed(const Neutral * pBlocking);
	index									Index() const			{ return m_index; }
	const ChokePoint *						PathBackTrace() const							{ return m_pPathBackTrace; }
	void									SetPathBackTrace(const ChokePoint * p) const	{ m_pPathBackTrace = p; }

private:
	const detail::Graph *					GetGraph() const		{ return m_pGraph; }
	detail::Graph *							GetGraph()				{ return m_pGraph; }

	detail::Graph * const								m_pGraph;
	const bool											m_pseudo;
	const index											m_index;
	const std::pair<const Area *, const Area *>			m_Areas;
	BWAPI::WalkPosition									m_nodes[node_count];
	std::pair<BWAPI::WalkPosition, BWAPI::WalkPosition>	m_nodesInArea[node_count];
	const std::deque<BWAPI::WalkPosition>				m_Geometry;
	bool												m_blocked;
	Neutral *											m_pBlockingNeutral;
	mutable const ChokePoint *							m_pPathBackTrace = nullptr;
};


typedef ChokePoint::Path CPPath;



} // namespace BWEM


#endif

