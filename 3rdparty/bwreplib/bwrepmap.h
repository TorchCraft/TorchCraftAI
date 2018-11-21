//----------------------------------------------------------------------------------------------------
// Replay Map - jca (May 2003)
//----------------------------------------------------------------------------------------------------
// Classes you should use:
//
// class BWrepMap
// Check out http://www.starcraftai.com/wiki/CHK_Format
//----------------------------------------------------------------------------------------------------
#ifndef INC_BWREPMAP_H
#define INC_BWREPMAP_H
#include <string.h>

//----------------------------------------------------------------------------------------------------

// section info
class BWrepMapSection
{
public:
	const char *GetTitle() const {return m_title;}
	uint32_t GetSize() const {return m_size;}
	const unsigned char *GetData() const {return m_data;}

	//-int32_ternal
	void Init(const char *title, uint32_t size, unsigned const char *data)
		{memcpy(m_title,title,MAXTITLE); m_title[MAXTITLE]=0; m_size=size; m_data=data;}

private:
	enum {MAXTITLE=4};
	char m_title[MAXTITLE+1];
	uint32_t m_size;
	const unsigned char *m_data;     // pointer to data
};

//----------------------------------------------------------------------------------------------------

// Unit section info
class BWrepMapSectionUNIT : public BWrepMapSection
{
public:
	typedef enum
	{
		UNIT_STARTLOCATION=214,
		UNIT_MINERAL1=176,
		UNIT_MINERAL2=177,
		UNIT_MINERAL3=178,
		UNIT_GEYSER=188
	} eMAPUNITID;

	class BWrepUnitDesc
	{
	public:
		#pragma pack(push, 1)
		uint16_t d1;
		uint16_t d2;
		uint16_t x; // x32
		uint16_t y; // x32
		uint16_t unitid;  // value from eMAPUNITID
		unsigned char bytes1[6];
		unsigned char playerid;
		unsigned char bytes2[3];
		uint16_t mineral; // for mineral or geyser
		unsigned char bytes3[14];
		#pragma pack(pop)
	};

	int32_t GetUnitCount() const {return GetSize()/sizeof(BWrepUnitDesc);}
	BWrepUnitDesc* GetUnitDesc(int32_t i) const {return (BWrepUnitDesc*)(GetData()+i*sizeof(BWrepUnitDesc));}
};

//----------------------------------------------------------------------------------------------------

#define SECTION_TILE    "TILE"
#define SECTION_ISOM    "ISOM"
#define SECTION_MTXM    "MTXM"
#define SECTION_UNIT    "UNIT"
#define SECTION_TILESET "ERA"

// map info
class BWrepMap
{
public:
	BWrepMap() : m_data(0), m_datasize(0), m_sectionCount(0) {}
	~BWrepMap();

	// map dimensions
	int32_t GetWidth() const {return m_mapWidth;}
	int32_t GetHeight() const {return m_mapHeight;}

	// find section by name
	const BWrepMapSection* GetSection(const char *name) const;

	// get tile section info (2 bytes per map square)
	const BWrepMapSection* GetTileSection() const;

	//-int32_ternal
	bool DecodeMap(const unsigned char *buffer, int32_t mapSize, int32_t w, int32_t h);

private:
	const unsigned char *m_data;     // point32_ter to data
	int32_t m_datasize;	// data size

	// sections
	enum {MAXSECTION=36};
	BWrepMapSection m_sections[MAXSECTION];
	int32_t m_sectionCount;

	// map dimensions
	int32_t m_mapWidth;
	int32_t m_mapHeight;

	// clear current info
	void _Clear();
};

//----------------------------------------------------------------------------------------------------

#endif // INC_BWREPMAP_H
