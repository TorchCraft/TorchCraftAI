//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_GRID_MAP_H
#define BWEM_GRID_MAP_H

#include "BWAPI.h"
#include <vector>
#include "map.h"
#include "utils.h"
#include "defs.h"


namespace BWEM
{

class Map;


namespace utils {


//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class GridMap
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
// A basic and generic "grid map" class that works well with the BWEM Library.
// The grid is composed of cells whose type T is user defined.
// Each cell matches a square of N*N tiles.
// The idea is that all the data stored in a cell can be accessed in O(1).
//
// Choose N high enough to efficiently divide the space of the Map.
// Choose N low enough to efficiently performs operations inside each Cell.
//
// You can create any number of GridMap instances, with the same or distinct values for N.
// Keep in mind that even if well designed, a GridMap (as any container) cannot fit all your needs generally.
//
// A typical use case would be to store the BWAPI units in a GridMap.
// See SimpleGridMap in example.h
//

template<class T, int N>
class GridMap
{
public:
	typedef T Cell;
	enum {cell_width_in_tiles = N};
	enum {tiles_per_cell = cell_width_in_tiles * cell_width_in_tiles};

								GridMap(const Map * pMap);

	// Returns the size of the GridMap in Cell number.
	int							Width() const						{ return m_width; }
	int							Height() const						{ return m_height; }

	// Returns a Cell, given its coordinates
	const Cell &				GetCell(int i, int j, check_t checkMode = check_t::check) const					{ bwem_assert((checkMode == check_t::no_check) || ValidCoords(i, j)); utils::unused(checkMode); return m_Cells[m_width * j + i]; }
	Cell &						GetCell(int i, int j, check_t checkMode = check_t::check)							{ bwem_assert((checkMode == check_t::no_check) || ValidCoords(i, j)); utils::unused(checkMode); return m_Cells[m_width * j + i]; }

	// Returns the Cell thats contains the Tile t
	const Cell &				GetCell(const BWAPI::TilePosition & t, check_t checkMode = check_t::check) const	{ bwem_assert((checkMode == check_t::no_check) || m_pMap->Valid(t)); utils::unused(checkMode); return GetCell(t.x/N, t.y/N, check_t::no_check); }
	Cell &						GetCell(const BWAPI::TilePosition & t, check_t checkMode = check_t::check)		{ bwem_assert((checkMode == check_t::no_check) || m_pMap->Valid(t)); utils::unused(checkMode); return GetCell(t.x/N, t.y/N, check_t::no_check); }

	// Returns the coordinates of the Cell thats contains the Tile t
	std::pair<int, int>			GetCellCoords(const BWAPI::TilePosition & t, check_t checkMode = check_t::check) const	{ bwem_assert((checkMode == check_t::no_check) || m_pMap->Valid(t)); utils::unused(checkMode); return std::make_pair(t.x/N, t.y/N); }

	// Returns specific tiles of a Cell, given its coordinates.
	BWAPI::TilePosition			GetTopLeft(int i, int j, check_t checkMode = check_t::check) const				{ bwem_assert((checkMode == check_t::no_check) || ValidCoords(i, j)); utils::unused(checkMode); return BWAPI::TilePosition(i*N, j*N); }
	BWAPI::TilePosition			GetBottomRight(int i, int j, check_t checkMode = check_t::check) const			{ bwem_assert((checkMode == check_t::no_check) || ValidCoords(i, j)); utils::unused(checkMode); return BWAPI::TilePosition((i+1)*N, (j+1)*N) - BWAPI::TilePosition(1, 1); }
	BWAPI::TilePosition			GetCenter(int i, int j, check_t checkMode = check_t::check) const					{ bwem_assert((checkMode == check_t::no_check) || ValidCoords(i, j)); utils::unused(checkMode); return BWAPI::TilePosition(i*N, j*N) + BWAPI::TilePosition(N/2, N/2); }

	// Provides access to the internal array of Cells.
	const std::vector<Cell> &	Cells() const						{ return m_Cells; }

	// Returns whether the coordinates (i, j) is valid.
	bool						ValidCoords(int i, int j) const		{ return (0 <= i) && (i < Width()) && (0 <= j) && (j < Height()); }

private:
	const Map *					m_pMap;

	int							m_width;
	int							m_height;
	std::vector<Cell>			m_Cells;
};


template<class T, int N>
GridMap<T, N>::GridMap(const Map * pMap)
: m_pMap(pMap),
	m_width(pMap->Size().x / N),
	m_height(pMap->Size().y / N),
	m_Cells(m_width * m_height)
{
	static_assert(N > 0, "GridMap::cell_width_in_tiles must be > 0");
	bwem_assert_throw(pMap->Initialized());
	bwem_assert_throw(N <= std::min(pMap->Size().x, pMap->Size().y));
	bwem_assert_throw(pMap->Size().x % N == 0);
	bwem_assert_throw(pMap->Size().y % N == 0);


}


}} // namespace BWEM::utils


#endif

