//////////////////////////////////////////////////////////////////////////
//
// This file is part of the BWEM Library.
// BWEM is free software, licensed under the MIT/X11 License. 
// A copy of the license is provided with the library in the LICENSE file.
// Copyright (c) 2015, 2016, Igor Dimitrijevic
//
//////////////////////////////////////////////////////////////////////////




#ifndef BWEM_UTILS_H
#define BWEM_UTILS_H


#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <fstream>
#include <sstream>
#include "defs.h"


namespace BWEM {


namespace utils {



//extern std::ofstream Log;




template <class T> void unused(const T &) {}



inline int queenWiseNorm(int dx, int dy)
{
	return std::max(abs(dx), abs(dy));
}


inline int squaredNorm(int dx, int dy)
{
	return dx*dx + dy*dy;
}


inline double norm(int dx, int dy)
{
	return sqrt(squaredNorm(dx, dy));
}


inline int scalar_product(int ax, int ay, int bx, int by)
{
	return ax*bx + ay*by;
}


// Returns whether the line segments [a, b] and [c, d] intersect.
bool intersect(int ax, int ay, int bx, int by, int cx, int cy, int dx, int dy);


template<class T>
std::string my_to_string(const T & value)
{
    std::ostringstream oss;
    std::string res;
    oss << value;
    res = oss.str();
    return res;
}


template<class T>
T string_to_value(const std::string & s)
{
	std::istringstream iss(s);
	T res;
	iss >> res;
	return res;
}


template<typename T>
bool from_string(const std::string & s, T & dest)
{
	std::istringstream iss(s);
	return !(iss >> dest).fail();
}


bool canWrite(const std::string & fileName);

// http://stackoverflow.com/questions/17224256/function-checking-if-an-integer-type-can-fit-a-value-of-possibly-different-inte
template <typename T, typename U>
bool CanTypeFitValue(const U value) {
    const intmax_t botT = intmax_t(std::numeric_limits<T>::min() );
    const intmax_t botU = intmax_t(std::numeric_limits<U>::min() );
    const uintmax_t topT = uintmax_t(std::numeric_limits<T>::max() );
    const uintmax_t topU = uintmax_t(std::numeric_limits<U>::max() );
    return !( (botT > botU && value < static_cast<U> (botT)) || (topT < topU && value > static_cast<U> (topT)) );        
}


// http://stackoverflow.com/questions/6942273/get-random-element-from-container-c-stl
template <typename I>
I random_element(I begin, I end)
{
    const auto n = std::distance(begin, end);
    const auto divisor = (RAND_MAX + 1) / n;

	typename std::remove_const<decltype(n)>::type k;
    do { k = std::rand() / divisor; } while (k >= n);

    std::advance(begin, k);
	return begin;
}


template<class T>
inline const typename T::value_type & random_element(const T & Container)
{
    const auto n = Container.size();
    const auto divisor = (RAND_MAX + 1) / n;

	typename std::remove_const<decltype(n)>::type k;
    do { k = std::rand() / divisor; } while (k >= n);

	return Container[k];
}


template<class T>
inline typename T::value_type & random_element(T & Container)
{
	return const_cast<typename T::value_type &>(random_element(static_cast<const T &>(Container)));
}


template<class T>
inline void really_remove(T & Container, const typename T::value_type & Element)
{
    Container.erase(remove(Container.begin(), Container.end(), Element), Container.end());
}


template<class T, class Pred>
inline void really_remove_if(T & Container, Pred pred)
{
    Container.erase(remove_if(Container.begin(), Container.end(), pred), Container.end());
}


template<class T, class E>
inline bool contains(const T & Container, const E & Element)
{
    return find(Container.begin(), Container.end(), Element) != Container.end();
}




template<class T>
inline void fast_erase(std::vector<T> & Vector, int i)
{
	bwem_assert((0 <= i) && (i < (int)Vector.size()));

	std::swap(Vector[i], Vector.back());
	Vector.pop_back();
}


struct compare2nd
{
    template <typename T>
    bool operator()(const T & a, const T & b)
    {
        return a.second < b.second;
    }
};


//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class Markable
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
//  Enables marking instances with a specific value.
//
//  Usage: class MyNode : (public) Markable<MyNode, unsigned> {...};
//

template<class Derived, class Mark>
class Markable
{
public:
    typedef Mark mark_t;

    Markable() : m_lastMark(0) {}

    void SetMarked(mark_t mark) const    { m_lastMark = mark; }
    bool IsMarkedWith(mark_t mark) const { return m_lastMark == mark; }

private:
    mutable mark_t m_lastMark;
};





//////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                          //
//                                  class UserData
//                                                                                          //
//////////////////////////////////////////////////////////////////////////////////////////////
//
//  Provides free-to-use, intrusive data for several types of the BWEM library
//	Despite their names and their types, they can be used for any purpose.
//

class UserData
{
public:
	// Free use.
	int					Data() const					{ return m_data; }
	void				SetData(int data) const			{ m_data = data; }

	// Free use.
	void *				Ptr() const						{ return m_ptr; }
	void				SetPtr(void * p) const			{ m_ptr = p; }

	// Free use.
	void *				Ext() const						{ return m_ext; }
	void				SetExt(void * p) const			{ m_ext = p; }

protected:
						UserData() = default;
						UserData(const UserData &) = default;

private:
	mutable void *		m_ptr = nullptr;
	mutable void *		m_ext = nullptr;
	mutable int			m_data = 0;
};





}} // namespace BWEM::utils









#endif

