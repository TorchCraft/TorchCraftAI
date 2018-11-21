//----------------------------------------------------------------------------------------------------
// Replay Map - jca (May 2003)
//----------------------------------------------------------------------------------------------------
#include "bwrepapi.h"
#include "bwrepactions.h"
#include "bwrepgamedata.h"
#include "bwrepmap.h"

#include "unpack.h"
#include <assert.h>
#include <stdexcept>

//------------------------------------------------------------------------------------------------------------

//
bool BWrepMap::DecodeMap(const unsigned char *buffer, int32_t mapSize, int w, int h)
{
	const unsigned char *current = buffer;
	int32_t read=0;

	// map dimensions
	m_mapWidth=w;
	m_mapHeight=h;

	// clear previous infos
	_Clear();

	// read block chain
	while(read<mapSize && m_sectionCount<MAXSECTION)
	{
		// extract block title
		char blockTitle[5];
		memcpy(blockTitle,current,4);
		blockTitle[4]=0;
		current+=4;

		// extract block size
		uint32_t bsize = *((uint32_t*)current);
		current+=4;

		// init section block
		read+=8;
		m_sections[m_sectionCount].Init(blockTitle,bsize,current);

		// next block
		m_sectionCount++;
		current+=bsize;
		read+=bsize;
	}

	// keep pointer on buffer
	m_data = buffer;
	m_datasize = mapSize;

	return true;
}

//------------------------------------------------------------------------------------------------------------

void BWrepMap::_Clear()
{
	m_sectionCount=0;
	m_datasize=0;

	// free data buffer
	if(m_data!=0) free((void*)m_data);
	m_data=0;
}

//------------------------------------------------------------------------------------------------------------

BWrepMap::~BWrepMap()
{
	_Clear();
}

//------------------------------------------------------------------------------------------------------------

// find section by name
const BWrepMapSection* BWrepMap::GetSection(const char *name) const
{
#ifdef _MSC_VER
	throw std::runtime_error("BWrepMap::GetSection is not supported in MSVC/Windows due to use of strcasecmp, a non-standard case-insensitive string comparison.");
#else
	for(int32_t i=0; i<m_sectionCount; i++)
	{
		if (strcasecmp(name, m_sections[i].GetTitle()) == 0) {
			return &m_sections[i];
		}
	}
	return 0;
#endif
}

//------------------------------------------------------------------------------------------------------------

// get tile section info (2 bytes per map square)
const BWrepMapSection* BWrepMap::GetTileSection() const
{
	// depends on the replay file format
	const BWrepMapSection *tile= GetSection(SECTION_TILE);
	if(tile==0) tile = GetSection(SECTION_MTXM);
	return tile;
}

//------------------------------------------------------------------------------------------------------------
