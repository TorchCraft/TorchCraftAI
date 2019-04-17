//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////




#include "utils.h"
#include <assert.h>

using namespace std;

namespace BWEM {
namespace utils {


//ofstream Log("bwapi-data/write/log.txt");


bool canWrite(const string & fileName)
{
	ofstream out(fileName);
	out.close();
	return !out.fail();
}




// From http://stackoverflow.com/questions/563198/how-do-you-detect-where-two-line-segments-intersect

// Returns true if the lines intersect, otherwise false. In addition, if the lines 
// intersect the intersection point may be stored in i_x and i_y.
static bool get_line_intersection(double p0_x, double p0_y, double p1_x, double p1_y, 
    double p2_x, double p2_y, double p3_x, double p3_y, double *i_x = nullptr, double *i_y = nullptr)
{
    double s1_x, s1_y, s2_x, s2_y;
    s1_x = p1_x - p0_x;     s1_y = p1_y - p0_y;
    s2_x = p3_x - p2_x;     s2_y = p3_y - p2_y;

    double s, t;
    s = (-s1_y * (p0_x - p2_x) + s1_x * (p0_y - p2_y)) / (-s2_x * s1_y + s1_x * s2_y);
    t = ( s2_x * (p0_y - p2_y) - s2_y * (p0_x - p2_x)) / (-s2_x * s1_y + s1_x * s2_y);

    if (s >= 0 && s <= 1 && t >= 0 && t <= 1)
    {
        // Collision detected
        if (i_x != nullptr) *i_x = p0_x + (t * s1_x);
        if (i_y != nullptr) *i_y = p0_y + (t * s1_y);
        return true;
    }

    return false; // No collision
}


bool intersect(int ax, int ay, int bx, int by, int cx, int cy, int dx, int dy)
{
	return get_line_intersection(ax, ay, bx, by, cx, cy, dx, dy);
}


}} // namespace BWEM::utils

