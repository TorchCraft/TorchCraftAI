//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License.
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#include "mapImpl.h"
#include "neutral.h"
#include "bwapiExt.h"


using namespace BWAPI;
using namespace BWAPI::UnitTypes::Enum;

using namespace std;


namespace BWEM {
namespace detail {


static bool adjoins8SomeLakeOrNeutral(WalkPosition p, const MapImpl * pMap)
{
	for (WalkPosition delta : { WalkPosition(-1, -1), WalkPosition(0, -1), WalkPosition(+1, -1),
								WalkPosition(-1,  0),                      WalkPosition(+1,  0),
								WalkPosition(-1, +1), WalkPosition(0, +1), WalkPosition(+1, +1)})
	{
		WalkPosition next = p + delta;
		if (pMap->Valid(next))
		{
			if (pMap->GetTile(TilePosition(next), check_t::no_check).GetNeutral()) return true;
			if (pMap->GetMiniTile(next, check_t::no_check).Lake()) return true;
		}
	}

	return false;
}





//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class MapImpl
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////

MapImpl::MapImpl()
: m_Graph(this)
{

}


MapImpl::~MapImpl()
{
	m_automaticPathUpdate = false;		// now there is no need to update the paths
}


void MapImpl::Initialize(BWAPI::Game* bw)
{
///	Timer overallTimer;
///	Timer timer;

	m_Size = TilePosition(bw->mapWidth(), bw->mapHeight());
	m_size = Size().x * Size().y;
	m_Tiles.resize(m_size);

	m_WalkSize = WalkPosition(Size());
	m_walkSize = WalkSize().x * WalkSize().y;
	m_MiniTiles.resize(m_walkSize);

	m_center = Position(Size())/2;

	for (TilePosition t : bw->getStartLocations())
		m_StartingLocations.push_back(t);

///	bw << "Map::Initialize-resize: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	LoadData(bw);
///	bw << "Map::LoadData: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	DecideSeasOrLakes();
///	bw << "Map::DecideSeasOrLakes: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	InitializeNeutrals(bw);
///	bw << "Map::InitializeNeutrals: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	ComputeAltitude();
///	bw << "Map::ComputeAltitude: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	ProcessBlockingNeutrals();
///	bw << "Map::ProcessBlockingNeutrals: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	ComputeAreas();
///	bw << "Map::ComputeAreas: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	GetGraph().CreateChokePoints();
///	bw << "Graph::CreateChokePoints: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	GetGraph().ComputeChokePointDistanceMatrix();
///	bw << "Graph::ComputeChokePointDistanceMatrix: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	GetGraph().CollectInformation();
///	bw << "Graph::CollectInformation: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

	GetGraph().CreateBases();
///	bw << "Graph::CreateBases: " << timer.ElapsedMilliseconds() << " ms" << endl; timer.Reset();

///	bw << "Map::Initialize: " << overallTimer.ElapsedMilliseconds() << " ms" << endl;
}


void MapImpl::LoadData(BWAPI::Game* bw)
{
	// Mark unwalkable minitiles (minitiles are walkable by default)
	for (int y = 0 ; y < WalkSize().y ; ++y)
	for (int x = 0 ; x < WalkSize().x ; ++x)
		if (!bw->isWalkable(x, y))						// For each unwalkable minitile, we also mark its 8 neighbours as not walkable.
			for (int dy = -1 ; dy <= +1 ; ++dy)			// According to some tests, this prevents from wrongly pretending one Marine can go by some thin path.
			for (int dx = -1 ; dx <= +1 ; ++dx)
			{
				WalkPosition w(x+dx, y+dy);
				if (Valid(w))
					GetMiniTile_(w, check_t::no_check).SetWalkable(false);
			}

	// Mark buildable tiles (tiles are unbuildable by default)
	for (int y = 0 ; y < Size().y ; ++y)
	for (int x = 0 ; x < Size().x ; ++x)
	{
		TilePosition t(x, y);
		if (bw->isBuildable(t))
		{
			GetTile_(t).SetBuildable();

			// Ensures buildable ==> walkable:
			for (int dy = 0 ; dy < 4 ; ++dy)
			for (int dx = 0 ; dx < 4 ; ++dx)
				GetMiniTile_(WalkPosition(t) + WalkPosition(dx, dy), check_t::no_check).SetWalkable(true);
		}

		// Add groundHeight and doodad information:
		int bwapiGroundHeight = bw->getGroundHeight(t);
		GetTile_(t).SetGroundHeight(bwapiGroundHeight / 2);
		if (bwapiGroundHeight % 2)
			GetTile_(t).SetDoodad();
	}
}


void MapImpl::DecideSeasOrLakes()
{
	for (int y = 0 ; y < WalkSize().y ; ++y)
	for (int x = 0 ; x < WalkSize().x ; ++x)
	{
		WalkPosition origin = WalkPosition(x, y);
		MiniTile & Origin = GetMiniTile_(origin, check_t::no_check);
		if (Origin.SeaOrLake())
		{
			vector<WalkPosition> ToSearch{origin};
			vector<MiniTile *> SeaExtent{&Origin};
			Origin.SetSea();
			WalkPosition topLeft = origin;
			WalkPosition bottomRight = origin;
			while (!ToSearch.empty())
			{
				WalkPosition current = ToSearch.back();
				if (current.x < topLeft.x) topLeft.x = current.x;
				if (current.y < topLeft.y) topLeft.y = current.y;
				if (current.x > bottomRight.x) bottomRight.x = current.x;
				if (current.y > bottomRight.y) bottomRight.y = current.y;

				ToSearch.pop_back();
				for (WalkPosition delta : {WalkPosition(0, -1), WalkPosition(-1, 0), WalkPosition(+1, 0), WalkPosition(0, +1)})
				{
					WalkPosition next = current + delta;
					if (Valid(next))
					{
						MiniTile & Next = GetMiniTile_(next, check_t::no_check);
						if (Next.SeaOrLake())
						{
							ToSearch.push_back(next);
							if (SeaExtent.size() <= lake_max_miniTiles) SeaExtent.push_back(&Next);
							Next.SetSea();
						}
					}
				}
			}

			if ((SeaExtent.size() <= lake_max_miniTiles) &&
				(bottomRight.x - topLeft.x <= lake_max_width_in_miniTiles) &&
				(bottomRight.y - topLeft.y <= lake_max_width_in_miniTiles) &&
				(topLeft.x >= 2) && (topLeft.y >= 2) && (bottomRight.x < WalkSize().x-2) && (bottomRight.y < WalkSize().y-2))
				for (MiniTile * pSea : SeaExtent)
					pSea->SetLake();
		}
	}
}


void MapImpl::InitializeNeutrals(BWAPI::Game* bw)
{
	for (auto n : bw->getStaticNeutralUnits())
	{
		if (n->getType().isBuilding())
		{
			if (n->getType().isMineralField())
			{
				m_Minerals.push_back(make_unique<Mineral>(n, this));
			}
			else if (n->getType() == Resource_Vespene_Geyser)
			{
				m_Geysers.push_back(make_unique<Geyser>(n, this));
			}
			else if (n->getType().isBuilding() && !n->isLifted())
			{
				m_StaticBuildings.push_back(make_unique<StaticBuilding>(n, this));
			}
		}
		else if (n->getType() != Zerg_Egg)
		{
			if (!n->getType().isCritter())
			{
				bwem_assert_plus(
					n->getType() == Special_Pit_Door ||
					n->getType() == Special_Right_Pit_Door ||
					false, n->getType().getName());

				if (n->getType() == Special_Pit_Door)
					m_StaticBuildings.push_back(make_unique<StaticBuilding>(n, this));
				if (n->getType() == Special_Right_Pit_Door)
					m_StaticBuildings.push_back(make_unique<StaticBuilding>(n, this));
			}
		}
	}
}


void MapImpl::ReplaceAreaIds(BWAPI::WalkPosition p, Area::id newAreaId)
{
	MiniTile & Origin = GetMiniTile_(p, check_t::no_check);
	Area::id oldAreaId = Origin.AreaId();
	Origin.ReplaceAreaId(newAreaId);

	vector<WalkPosition> ToSearch{p};
	while (!ToSearch.empty())
	{
		WalkPosition current = ToSearch.back();

		ToSearch.pop_back();
		for (WalkPosition delta : {WalkPosition(0, -1), WalkPosition(-1, 0), WalkPosition(+1, 0), WalkPosition(0, +1)})
		{
			WalkPosition next = current + delta;
			if (Valid(next))
			{
				MiniTile & Next = GetMiniTile_(next, check_t::no_check);
				if (Next.AreaId() == oldAreaId)
				{
					ToSearch.push_back(next);
					Next.ReplaceAreaId(newAreaId);
				}
			}
		}
	}

	// also replaces references of oldAreaId by newAreaId in m_RawFrontier:
	if (newAreaId > 0)
		for (auto & f : m_RawFrontier)
		{
			if (f.first.first == oldAreaId) f.first.first = newAreaId;
			if (f.first.second == oldAreaId) f.first.second = newAreaId;
		}
}


// Assigns MiniTile::m_altitude foar each miniTile having AltitudeMissing()
// Cf. MiniTile::Altitude() for meaning of altitude_t.
// Altitudes are computed using the straightforward Dijkstra's algorithm : the lower ones are computed first, starting from the seaside-miniTiles neighbours.
// The point here is to precompute all possible altitudes for all possible tiles, and sort them.
void MapImpl::ComputeAltitude()
{
	const int altitude_scale = 8;	// 8 provides a pixel definition for altitude_t, since altitudes are computed from miniTiles which are 8x8 pixels

	// 1) Fill in and sort DeltasByAscendingAltitude
	const int range = max(WalkSize().x, WalkSize().y) / 2 + 3;		// should suffice for maps with no Sea.

	vector<pair<WalkPosition, altitude_t>> DeltasByAscendingAltitude;

	for (int dy = 0 ; dy <= range ; ++dy)
	for (int dx = dy ; dx <= range ; ++dx)			// Only consider 1/8 of possible deltas. Other ones obtained by symmetry.
		if (dx || dy)
			DeltasByAscendingAltitude.emplace_back(WalkPosition(dx, dy), altitude_t(0.5 + norm(dx, dy) * altitude_scale));

	sort(DeltasByAscendingAltitude.begin(), DeltasByAscendingAltitude.end(),
		[](const pair<WalkPosition, altitude_t> & a, const pair<WalkPosition, altitude_t> & b){ return a.second < b.second; });


	// 2) Fill in ActiveSeaSideList, which basically contains all the seaside miniTiles (from which altitudes are to be computed)
	//    It also includes extra border-miniTiles which are considered as seaside miniTiles too.
	struct ActiveSeaSide { WalkPosition origin; altitude_t lastAltitudeGenerated; };
	vector<ActiveSeaSide> ActiveSeaSideList;

	for (int y = -1 ; y <= WalkSize().y ; ++y)
	for (int x = -1 ; x <= WalkSize().x ; ++x)
	{
		WalkPosition w(x, y);
		if (!Valid(w) || seaSide(w, this))
			ActiveSeaSideList.push_back(ActiveSeaSide{w, 0});
	}

	// 3) Dijkstra's algorithm
	for (const auto & delta_altitude : DeltasByAscendingAltitude)
	{
		const WalkPosition d = delta_altitude.first;
		const altitude_t altitude = delta_altitude.second;
		for (int i = 0 ; i < (int)ActiveSeaSideList.size() ; ++i)
		{
			ActiveSeaSide & Current = ActiveSeaSideList[i];
			if (altitude - Current.lastAltitudeGenerated >= 2 * altitude_scale)		// optimization : once a seaside miniTile verifies this condition,
				fast_erase(ActiveSeaSideList, i--);									// we can throw it away as it will not generate min altitudes anymore
			else
				for (auto delta : {	WalkPosition(d.x, d.y), WalkPosition(-d.x, d.y), WalkPosition(d.x, -d.y), WalkPosition(-d.x, -d.y),
									WalkPosition(d.y, d.x), WalkPosition(-d.y, d.x), WalkPosition(d.y, -d.x), WalkPosition(-d.y, -d.x)})
				{
					WalkPosition w = Current.origin + delta;
					if (Valid(w))
					{
						auto & miniTile = GetMiniTile_(w, check_t::no_check);
						if (miniTile.AltitudeMissing())
							miniTile.SetAltitude(m_maxAltitude = Current.lastAltitudeGenerated = altitude);
					}
				}
		}
	}
}


void MapImpl::ProcessBlockingNeutrals()
{
	vector<Neutral *> Candidates;
	for (auto & s : StaticBuildings())	Candidates.push_back(s.get());
	for (auto & m : Minerals())			Candidates.push_back(m.get());

	for (Neutral * pCandidate : Candidates)
		if (!pCandidate->NextStacked())		// in the case where several neutrals are stacked, we only consider the top one
		{
			// 1)  Retreave the Border: the outer border of pCandidate
			vector<WalkPosition> Border = outerMiniTileBorder(pCandidate->TopLeft(), pCandidate->Size());
			really_remove_if(Border, [this](WalkPosition w)	{
				return !Valid(w) || !GetMiniTile(w, check_t::no_check).Walkable() ||
					GetTile(TilePosition(w), check_t::no_check).GetNeutral(); });

			// 2)  Find the doors in Border: one door for each connected set of walkable, neighbouring miniTiles.
			//     The searched connected miniTiles all have to be next to some lake or some static building, though they can't be part of one.
			vector<WalkPosition> Doors;
			while (!Border.empty())
			{
				WalkPosition door = Border.back(); Border.pop_back();
				Doors.push_back(door);
				vector<WalkPosition> ToVisit(1, door);
				vector<WalkPosition> Visited(1, door);
				while (!ToVisit.empty())
				{
					WalkPosition current = ToVisit.back(); ToVisit.pop_back();
					for (WalkPosition delta : {WalkPosition(0, -1), WalkPosition(-1, 0), WalkPosition(+1, 0), WalkPosition(0, +1)})
					{
						WalkPosition next = current + delta;
						if (Valid(next) && !contains(Visited, next))
							if (GetMiniTile(next, check_t::no_check).Walkable())
								if (!GetTile(TilePosition(next), check_t::no_check).GetNeutral())
									if (adjoins8SomeLakeOrNeutral(next, this))
									{
										ToVisit.push_back(next);
										Visited.push_back(next);
									}
					}
				}
				really_remove_if(Border, [&Visited](WalkPosition w)	{ return contains(Visited, w); });
			}

			// 3)  If at least 2 doors, find the true doors in Border: a true door is a door that gives onto an area big enough
			vector<WalkPosition> TrueDoors;
			if (Doors.size() >= 2)
				for (WalkPosition door : Doors)
				{
					vector<WalkPosition> ToVisit(1, door);
					vector<WalkPosition> Visited(1, door);
					const size_t limit = pCandidate->IsStaticBuilding() ? 10 : 400;
					while (!ToVisit.empty() && (Visited.size() < limit))
					{
						WalkPosition current = ToVisit.back(); ToVisit.pop_back();
						for (WalkPosition delta : {WalkPosition(0, -1), WalkPosition(-1, 0), WalkPosition(+1, 0), WalkPosition(0, +1)})
						{
							WalkPosition next = current + delta;
							if (Valid(next) && !contains(Visited, next))
								if (GetMiniTile(next, check_t::no_check).Walkable())
									if (!GetTile(TilePosition(next), check_t::no_check).GetNeutral())
									{
										ToVisit.push_back(next);
										Visited.push_back(next);
									}
						}
					}
					if (Visited.size() >= limit) TrueDoors.push_back(door);
				}

			// 4)  If at least 2 true doors, pCandidate is a blocking static building
			if (TrueDoors.size() >= 2)
			{
				// Marks pCandidate (and any Neutral stacked with it) as blocking.
				for (Neutral * pNeutral = GetTile(pCandidate->TopLeft()).GetNeutral() ; pNeutral ; pNeutral = pNeutral->NextStacked())
					pNeutral->SetBlocking(TrueDoors);

				// Marks all the miniTiles of pCandidate as blocked.
				// This way, areas at TrueDoors won't merge together.
				for (int dy = 0 ; dy < WalkPosition(pCandidate->Size()).y ; ++dy)
				for (int dx = 0 ; dx < WalkPosition(pCandidate->Size()).x ; ++dx)
				{
					auto & miniTile = GetMiniTile_(WalkPosition(pCandidate->TopLeft()) + WalkPosition(dx, dy));
					if (miniTile.Walkable()) miniTile.SetBlocked();
				}
			}
		}
}


// Helper class for void Map::ComputeAreas()
// Maintains some information about an area being computed
// A TempAreaInfo is not Valid() in two cases:
//   - a default-constructed TempAreaInfo instance is never Valid (used as a dummy value to simplify the algorithm).
//   - any other instance becomes invalid when absorbed (see Merge)
class TempAreaInfo
{
public:
						TempAreaInfo() : m_valid(false), m_id(0), m_top(0, 0), m_highestAltitude(0) { bwem_assert(!Valid());}
						TempAreaInfo(Area::id id, MiniTile * pMiniTile, WalkPosition pos)
							: m_valid(true), m_id(id), m_top(pos), m_size(0), m_highestAltitude(pMiniTile->Altitude())
														{ Add(pMiniTile); bwem_assert(Valid()); }

	bool				Valid() const					{ return m_valid; }
	Area::id			Id() const						{ bwem_assert(Valid()); return m_id; }
	WalkPosition		Top() const						{ bwem_assert(Valid()); return m_top; }
	int					Size() const					{ bwem_assert(Valid()); return m_size; }
	altitude_t			HighestAltitude() const			{ bwem_assert(Valid()); return m_highestAltitude; }

	void				Add(MiniTile * pMiniTile)		{ bwem_assert(Valid()); ++m_size; pMiniTile->SetAreaId(m_id); }

	// Left to caller : m.SetAreaId(this->Id()) for each MiniTile m in Absorbed
	void				Merge(TempAreaInfo & Absorbed)	{
															bwem_assert(Valid() && Absorbed.Valid());
															bwem_assert(m_size >= Absorbed.m_size);
															m_size += Absorbed.m_size;
															Absorbed.m_valid = false;
														}

	TempAreaInfo &		operator=(const TempAreaInfo &) = delete;

private:
	bool				m_valid;
	const Area::id		m_id;
	const WalkPosition	m_top;
	const altitude_t	m_highestAltitude;
	int					m_size;
};


// Assigns MiniTile::m_areaId for each miniTile having AreaIdMissing()
// Areas are computed using MiniTile::Altitude() information only.
// The miniTiles are considered successively in descending order of their Altitude().
// Each of them either:
//   - involves the creation of a new area.
//   - is added to some existing neighbouring area.
//   - makes two neighbouring areas merge together.
void MapImpl::ComputeAreas()
{
	vector<pair<WalkPosition, MiniTile *>> MiniTilesByDescendingAltitude = SortMiniTiles();

	vector<TempAreaInfo> TempAreaList = ComputeTempAreas(MiniTilesByDescendingAltitude);

	CreateAreas(TempAreaList);

	SetAreaIdInTiles();
}


vector<pair<WalkPosition, MiniTile *>> MapImpl::SortMiniTiles()
{
	vector<pair<WalkPosition, MiniTile *>> MiniTilesByDescendingAltitude;
	for (int y = 0 ; y < WalkSize().y ; ++y)
	for (int x = 0 ; x < WalkSize().x ; ++x)
	{
		WalkPosition w = WalkPosition(x, y);
		MiniTile & miniTile = GetMiniTile_(w, check_t::no_check);
		if (miniTile.AreaIdMissing())
			MiniTilesByDescendingAltitude.emplace_back(w, &miniTile);
	}

	sort(MiniTilesByDescendingAltitude.begin(), MiniTilesByDescendingAltitude.end(),
		[](const pair<WalkPosition, MiniTile *> & a, const pair<WalkPosition, MiniTile *> & b){ return a.second->Altitude() > b.second->Altitude(); });

	return MiniTilesByDescendingAltitude;
}


static pair<Area::id, Area::id> findNeighboringAreas(WalkPosition p, const MapImpl * pMap)
{
	pair<Area::id, Area::id> result(0, 0);

	for (WalkPosition delta : {WalkPosition(0, -1), WalkPosition(-1, 0), WalkPosition(+1, 0), WalkPosition(0, +1)})
		if (pMap->Valid(p + delta))
		{
			Area::id areaId = pMap->GetMiniTile(p + delta, check_t::no_check).AreaId();
			if (areaId > 0) {
				if (!result.first) result.first = areaId;
				else if (result.first != areaId)
					if (!result.second || ((areaId < result.second)))
						result.second = areaId;
			}
		}

	return result;
}


static Area::id chooseNeighboringArea(Area::id a, Area::id b)
{
	static thread_local map<pair<Area::id, Area::id>, int> map_AreaPair_counter;

	if (a > b) swap(a, b);
	return (map_AreaPair_counter[make_pair(a, b)]++ % 2 == 0) ? a : b;
}


vector<TempAreaInfo> MapImpl::ComputeTempAreas(const vector<pair<WalkPosition, MiniTile *>> & MiniTilesByDescendingAltitude)
{
	vector<TempAreaInfo> TempAreaList(1);		// TempAreaList[0] left unused, as AreaIds are > 0
	for (const auto & Current : MiniTilesByDescendingAltitude)
	{
		const WalkPosition pos = Current.first;
		MiniTile * cur = Current.second;

		pair<Area::id, Area::id> neighboringAreas = findNeighboringAreas(pos, this);
		if (!neighboringAreas.first)			// no neighboring area : creates of a new area
		{
			TempAreaList.emplace_back((Area::id)TempAreaList.size(), cur, pos);
		}
		else if (!neighboringAreas.second)		// one neighboring area : adds cur to the existing area
		{
			TempAreaList[neighboringAreas.first].Add(cur);
		}
		else									// two neighboring areas : adds cur to one of them  &  possible merging
		{
			Area::id smaller = neighboringAreas.first;
			Area::id bigger = neighboringAreas.second;
			if (TempAreaList[smaller].Size() > TempAreaList[bigger].Size()) swap(smaller, bigger);

			// Condition for the neighboring areas to merge:
			if ((TempAreaList[smaller].Size() < 80) ||
				(TempAreaList[smaller].HighestAltitude() < 80) ||
				(cur->Altitude() / (double)TempAreaList[bigger].HighestAltitude() >= 0.90) ||
				(cur->Altitude() / (double)TempAreaList[smaller].HighestAltitude() >= 0.90) ||
				any_of(StartingLocations().begin(), StartingLocations().end(), [&pos](const TilePosition & startingLoc)
					{ return dist(TilePosition(pos), startingLoc + TilePosition(2, 1)) <= 3;}) ||
				false
				)
			{
				// adds cur to the absorbing area:
				TempAreaList[bigger].Add(cur);

				// merges the two neighboring areas:
				ReplaceAreaIds(TempAreaList[smaller].Top(), bigger);
				TempAreaList[bigger].Merge(TempAreaList[smaller]);
			}
			else	// no merge : cur starts or continues the frontier between the two neighboring areas
			{
				// adds cur to the chosen Area:
				TempAreaList[chooseNeighboringArea(smaller, bigger)].Add(cur);
				m_RawFrontier.emplace_back(neighboringAreas, pos);
			}
		}
	}

	// Remove from the frontier obsolete positions
	really_remove_if(m_RawFrontier, [](const pair<pair<Area::id, Area::id>, BWAPI::WalkPosition> & f)
		{ return f.first.first == f.first.second; });

	return TempAreaList;
}


// Initializes m_Graph with the valid and big enough areas in TempAreaList.
void MapImpl::CreateAreas(const vector<TempAreaInfo> & TempAreaList)
{
	typedef pair<WalkPosition, int>	pair_top_size_t;
	vector<pair_top_size_t> AreasList;

	Area::id newAreaId = 1;
	Area::id newTinyAreaId = -2;

	for (auto & TempArea : TempAreaList)
		if (TempArea.Valid())
		{
			if (TempArea.Size() >= area_min_miniTiles)
			{
				bwem_assert(newAreaId <= TempArea.Id());
				if (newAreaId != TempArea.Id())
					ReplaceAreaIds(TempArea.Top(), newAreaId);

				AreasList.emplace_back(TempArea.Top(), TempArea.Size());
				newAreaId++;
			}
			else
			{
				ReplaceAreaIds(TempArea.Top(), newTinyAreaId);
				newTinyAreaId--;
			}
		}

	GetGraph().CreateAreas(AreasList);
}


void MapImpl::SetAreaIdInTile(TilePosition t)
{
	auto & tile = GetTile_(t);
	bwem_assert(tile.AreaId() == 0);	// initialized to 0

	for (int dy = 0 ; dy < 4 ; ++dy)
	for (int dx = 0 ; dx < 4 ; ++dx)
		if (Area::id id = GetMiniTile(WalkPosition(t) + WalkPosition(dx, dy), check_t::no_check).AreaId()) {
			if (tile.AreaId() == 0) tile.SetAreaId(id);
			else if (tile.AreaId() != id)
			{
				tile.SetAreaId(-1);
				return;
			}
		}
}


void MapImpl::SetAltitudeInTile(TilePosition t)
{
	altitude_t minAltitude = numeric_limits<altitude_t>::max();

	for (int dy = 0 ; dy < 4 ; ++dy)
	for (int dx = 0 ; dx < 4 ; ++dx)
	{
		altitude_t altitude = GetMiniTile(WalkPosition(t) + WalkPosition(dx, dy), check_t::no_check).Altitude();
		if (altitude < minAltitude)	minAltitude = altitude;
	}

	GetTile_(t).SetMinAltitude(minAltitude);
}


void MapImpl::SetAreaIdInTiles()
{
	for (int y = 0 ; y < Size().y ; ++y)
	for (int x = 0 ; x < Size().x ; ++x)
	{
		TilePosition t(x, y);
		SetAreaIdInTile(t);
		SetAltitudeInTile(t);
	}
}


Mineral * MapImpl::GetMineral(BWAPI::Unit u) const
{
	auto iMineral = find_if(m_Minerals.begin(), m_Minerals.end(), [u](const unique_ptr<Mineral> & m){ return m->Unit() == u; });
	return (iMineral != m_Minerals.end()) ? iMineral->get() : nullptr;
}


Geyser * MapImpl::GetGeyser(BWAPI::Unit u) const
{
	auto iGeyser = find_if(m_Geysers.begin(), m_Geysers.end(), [u](const unique_ptr<Geyser> & g){ return g->Unit() == u; });
	return (iGeyser != m_Geysers.end()) ? iGeyser->get() : nullptr;
}


void MapImpl::OnMineralDestroyed(BWAPI::Unit u)
{
	auto iMineral = find_if(m_Minerals.begin(), m_Minerals.end(), [u](const unique_ptr<Mineral> & m){ return m->Unit() == u; });
	bwem_assert_throw_plus(iMineral != m_Minerals.end(), "Can't find mineral " + std::to_string(u->getID()) + " in list");

	fast_erase(m_Minerals, distance(m_Minerals.begin(), iMineral));
}


void MapImpl::OnStaticBuildingDestroyed(BWAPI::Unit u)
{
	auto iStaticBuilding = find_if(m_StaticBuildings.begin(), m_StaticBuildings.end(), [u](const unique_ptr<StaticBuilding> & g){ return g->Unit() == u; });
	bwem_assert_throw_plus(iStaticBuilding != m_StaticBuildings.end(), "Can't find static building " + std::to_string(u->getID()) + " in list");

	fast_erase(m_StaticBuildings, distance(m_StaticBuildings.begin(), iStaticBuilding));
}


void MapImpl::OnMineralDestroyed(const Mineral * pMineral)
{
	for (Area & area : GetGraph().Areas())
		area.OnMineralDestroyed(pMineral);
}


void MapImpl::OnBlockingNeutralDestroyed(const Neutral * pBlocking)
{
	bwem_assert(pBlocking && pBlocking->Blocking());

	for (const Area * pArea : pBlocking->BlockedAreas())
		for (const ChokePoint * cp : pArea->ChokePoints())
			const_cast<ChokePoint *>(cp)->OnBlockingNeutralDestroyed(pBlocking);

	if (GetTile(pBlocking->TopLeft()).GetNeutral()) return;		// there remains some blocking Neutrals at the same location

	// Unblock the miniTiles of pBlocking:
	Area::id newId = pBlocking->BlockedAreas().front()->Id();
	for (int dy = 0 ; dy < WalkPosition(pBlocking->Size()).y ; ++dy)
	for (int dx = 0 ; dx < WalkPosition(pBlocking->Size()).x ; ++dx)
	{
		auto & miniTile = GetMiniTile_(WalkPosition(pBlocking->TopLeft()) + WalkPosition(dx, dy));
		if (miniTile.Walkable()) miniTile.ReplaceBlockedAreaId(newId);
	}

	// Unblock the Tiles of pBlocking:
	for (int dy = 0 ; dy < pBlocking->Size().y ; ++dy)
	for (int dx = 0 ; dx < pBlocking->Size().x ; ++dx)
	{
		GetTile_(pBlocking->TopLeft() + TilePosition(dx, dy)).ResetAreaId();
		SetAreaIdInTile(pBlocking->TopLeft() + TilePosition(dx, dy));
	}

	if (AutomaticPathUpdate())
		GetGraph().ComputeChokePointDistanceMatrix();
}


bool MapImpl::FindBasesForStartingLocations()
{
	bool atLeastOneFailed = false;
	for (auto location : StartingLocations())
	{
		bool found = false;
		for (Area & area : GetGraph().Areas()) if (!found)
			for (Base & base : area.Bases()) if (!found)
				if (queenWiseDist(base.Location(), location) <= max_tiles_between_StartingLocation_and_its_AssignedBase)
				{
					base.SetStartingLocation(location);
					found = true;
				}

		 if (!found) atLeastOneFailed = true;
	}

	return !atLeastOneFailed;
}


}} // namespace BWEM::detail
