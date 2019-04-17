//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////


#ifndef BWEM_BWAPI_EXT_H
#define BWEM_BWAPI_EXT_H

#include "BWAPI.h"
#include "utils.h"
#include "defs.h"
#include <vector>

namespace BWEM {


namespace BWAPI_ext {


template<typename T, int Scale = 1>
inline std::ostream & operator<<(std::ostream & out, BWAPI::Point<T, Scale> A)		{ out << "(" << A.x << ", " << A.y << ")"; return out; }


template<typename T, int Scale = 1>
inline BWAPI::Position center(BWAPI::Point<T, Scale> A)	{ return BWAPI::Position(A) + BWAPI::Position(Scale/2, Scale/2); }


template<typename T, int Scale = 1>
inline BWAPI::Point<T, Scale> operator+(BWAPI::Point<T, Scale> A, int b)	{ return A + BWAPI::Point<T, Scale>(b, b); }

template<typename T, int Scale = 1>
inline BWAPI::Point<T, Scale> operator+(int a, BWAPI::Point<T, Scale> B)	{ return B + a; }

template<typename T, int Scale = 1>
inline BWAPI::Point<T, Scale> operator-(BWAPI::Point<T, Scale> A, int b)		{ return A + (-b); }

template<typename T, int Scale = 1>
inline BWAPI::Point<T, Scale> operator-(int a, BWAPI::Point<T, Scale> B)		{ return a + (B*-1); }


// Enlarges the bounding box [TopLeft, BottomRight] so that it includes A.
template<typename T, int Scale = 1>
inline void makeBoundingBoxIncludePoint(BWAPI::Point<T, Scale> & TopLeft, BWAPI::Point<T, Scale> & BottomRight, const BWAPI::Point<T, Scale> & A)
{
	if (A.x < TopLeft.x)		TopLeft.x = A.x;
	if (A.x > BottomRight.x)	BottomRight.x = A.x;

	if (A.y < TopLeft.y)		TopLeft.y = A.y;
	if (A.y > BottomRight.y)	BottomRight.y = A.y;
}


// Makes the smallest change to A so that it is included in the bounding box [TopLeft, BottomRight].
template<typename T, int Scale = 1>
inline void makePointFitToBoundingBox(BWAPI::Point<T, Scale> & A, const BWAPI::Point<T, Scale> & TopLeft, const BWAPI::Point<T, Scale> & BottomRight)
{
	if		(A.x < TopLeft.x)		A.x = TopLeft.x;
	else if (A.x > BottomRight.x)	A.x = BottomRight.x;
	
	if		(A.y < TopLeft.y)		A.y = TopLeft.y;
	else if (A.y > BottomRight.y)	A.y = BottomRight.y;
}


template<typename T, int Scale = 1>
bool inBoundingBox(const BWAPI::Point<T, Scale> & A, const BWAPI::Point<T, Scale> & topLeft, const BWAPI::Point<T, Scale> & bottomRight)
{
	return  (A.x >= topLeft.x) && (A.x <= bottomRight.x) &&
			(A.y >= topLeft.y) && (A.y <= bottomRight.y);
}


template<typename T, int Scale = 1>
inline int queenWiseDist(BWAPI::Point<T, Scale> A, BWAPI::Point<T, Scale> B){ A -= B; return utils::queenWiseNorm(A.x, A.y); }

template<typename T, int Scale = 1>
inline int squaredDist(BWAPI::Point<T, Scale> A, BWAPI::Point<T, Scale> B)	{ A -= B; return squaredNorm(A.x, A.y); }

template<typename T, int Scale = 1>
inline double dist(BWAPI::Point<T, Scale> A, BWAPI::Point<T, Scale> B)		{ A -= B; return utils::norm(A.x, A.y); }

template<typename T, int Scale = 1>
inline int roundedDist(BWAPI::Point<T, Scale> A, BWAPI::Point<T, Scale> B)	{ return int(0.5 + dist(A, B)); }


inline int distToRectangle(const BWAPI::Position & a, const BWAPI::TilePosition & TopLeft, const BWAPI::TilePosition & Size)
{
	auto topLeft = BWAPI::Position(TopLeft);
	auto bottomRight = BWAPI::Position(TopLeft + Size) - 1;

	if (a.x >= topLeft.x)
		if (a.x <= bottomRight.x)
			if (a.y > bottomRight.y)	return a.y - bottomRight.y;											// S
			else if (a.y < topLeft.y)	return topLeft.y - a.y;												// N
			else						return 0;															// inside
		else
			if (a.y > bottomRight.y)	return roundedDist(a, bottomRight);									// SE
			else if (a.y < topLeft.y)	return roundedDist(a, BWAPI::Position(bottomRight.x, topLeft.y));	// NE
			else						return a.x - bottomRight.x;											// E
	else
		if (a.y > bottomRight.y)		return roundedDist(a, BWAPI::Position(topLeft.x, bottomRight.y));	// SW
		else if (a.y < topLeft.y)		return roundedDist(a, topLeft);										// NW
		else							return topLeft.x - a.x;												// W
}


template<typename T, int Scale = 1>
inline std::vector<BWAPI::Point<T, Scale>> innerBorder(BWAPI::Point<T, Scale> TopLeft, BWAPI::Point<T, Scale> Size, bool noCorner = false)
{
	std::vector<BWAPI::Point<T, Scale>> Border;
	for (int dy = 0 ; dy < Size.y ; ++dy)
	for (int dx = 0 ; dx < Size.x ; ++dx)
		if ((dy == 0) || (dy == Size.y-1) ||
			(dx == 0) || (dx == Size.x-1))
			if (!noCorner ||
				!(((dx == 0) && (dy == 0)) || ((dx == Size.x-1) && (dy == Size.y-1)) ||
				  ((dx == 0) && (dy == Size.y-1)) || ((dx == Size.x-1) && (dy == 0))))
			Border.push_back(TopLeft + BWAPI::Point<T, Scale>(dx, dy));

	return Border;
}

template<typename T, int Scale = 1>
inline std::vector<BWAPI::Point<T, Scale>> outerBorder(BWAPI::Point<T, Scale> TopLeft, BWAPI::Point<T, Scale> Size, bool noCorner = false)
{
	return innerBorder(TopLeft - 1, Size + 2, noCorner);
}


inline std::vector<BWAPI::WalkPosition> outerMiniTileBorder(BWAPI::TilePosition TopLeft, BWAPI::TilePosition Size, bool noCorner = false)
{
	return outerBorder(BWAPI::WalkPosition(TopLeft), BWAPI::WalkPosition(Size), noCorner);
}


inline std::vector<BWAPI::WalkPosition> innerMiniTileBorder(BWAPI::TilePosition TopLeft, BWAPI::TilePosition Size, bool noCorner = false)
{
	return innerBorder(BWAPI::WalkPosition(TopLeft), BWAPI::WalkPosition(Size), noCorner);
}


void drawDiagonalCrossMap(BWAPI::Position topLeft, BWAPI::Position bottomRight, BWAPI::Color col);


template<typename T, int Scale = 1>
inline bool overlap(const BWAPI::Point<T, Scale> & TopLeft1, const BWAPI::Point<T, Scale> & Size1, const BWAPI::Point<T, Scale> & TopLeft2, const BWAPI::Point<T, Scale> & Size2)
{
	if (TopLeft2.x >= TopLeft1.x + Size1.x) return false;
	if (TopLeft2.y >= TopLeft1.y + Size1.y) return false;
	if (TopLeft1.x >= TopLeft2.x + Size2.x) return false;
	if (TopLeft1.y >= TopLeft2.y + Size2.y) return false;
	return true;
}




}} // namespace BWEM::BWAPI_ext




#endif

