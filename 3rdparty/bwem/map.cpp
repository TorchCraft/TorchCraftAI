//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#include "map.h"
#include "mapImpl.h"
#include "bwapiExt.h"

using namespace BWAPI;
using namespace BWAPI::UnitTypes::Enum;

using namespace std;


namespace BWEM {

using namespace detail;
using namespace BWAPI_ext;

namespace utils {

bool seaSide(WalkPosition p, const Map * pMap)
{
	if (!pMap->GetMiniTile(p).Sea()) return false;

	for (WalkPosition delta : {WalkPosition(0, -1), WalkPosition(-1, 0), WalkPosition(+1, 0), WalkPosition(0, +1)})
		if (pMap->Valid(p + delta))
			if (!pMap->GetMiniTile(p + delta, check_t::no_check).Sea())
				return true;

	return false;
}

} // namespace utils


//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Map
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////

unique_ptr<Map> Map::m_gInstance = nullptr;


Map & Map::Instance()
{
	if (!m_gInstance) m_gInstance = make_unique<MapImpl>();

	return *m_gInstance.get();
}


Position Map::RandomPosition() const
{
	const auto PixelSize = Position(Size());
	return Position(rand() % PixelSize.x, rand() % PixelSize.y);
}


std::unique_ptr<Map> Map::Make()
{
  return std::make_unique<MapImpl>();
}


template<class TPosition>
TPosition crop(const TPosition & p, int siseX, int sizeY)
{
	TPosition res = p;

	if		(res.x < 0)			res.x = 0;
	else if (res.x >= siseX)	res.x = siseX-1;

	if		(res.y < 0)			res.y = 0;
	else if (res.y >= sizeY)	res.y = sizeY-1;

	return res;
}


TilePosition Map::Crop(const TilePosition & p) const
{
	return crop(p, Size().x, Size().y);
}


WalkPosition Map::Crop(const WalkPosition & p) const
{
	return crop(p, WalkSize().x, WalkSize().y);
}


Position Map::Crop(const Position & p) const
{
	return crop(p, 32*Size().x, 32*Size().y);
}


} // namespace BWEM



