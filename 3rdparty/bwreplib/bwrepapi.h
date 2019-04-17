//----------------------------------------------------------------------------------------------------
// Replay Loader - HurtnTime+jca+fractaled (May 2003)
//----------------------------------------------------------------------------------------------------
// Classes you should use:
//
// class BWrepFile   : object to use to load a replay
// class BWrepHeader : contains replay header (replay creator, map name, all players info, etc)
// class BWrepPlayer : contains information on a player (given by BWrepHeader object)
//----------------------------------------------------------------------------------------------------
#ifndef INC_BWREPAPI_H
#define INC_BWREPAPI_H

#include "bwrepactions.h"
#include "bwrepgamedata.h"
#include "bwrepmap.h"

#include <stdio.h>
#include <time.h>
#include <inttypes.h>

const int32_t kBWREP_PLAYERNAME_SIZE = 0x19;
const int32_t kBWREP_HEADER_SIZE	 = 0x279;
const int32_t kBWREP_ID		 = 0x53526572;
const int32_t kBWREP_NUM_PLAYERS	 = 12;
const int32_t kBWREP_NAME_SIZE		 = 24;
const int32_t kBWREP_MAPNAME_SIZE	 = 23;
const int32_t kBWREP_NUM_SLOT		 = 8;
const int32_t kBWREP_GNAME_SIZE      = 28;

#pragma pack(push, 1)

//
// player info
//
class BWrepPlayer
{
public:

	enum RACE
	{
		RACE_ZERG,
		RACE_TERRAN,
		RACE_PROTOSS,
		RACE_6=6			// TODO: figure out what this is
	};

	enum TYPE
	{
		TYPE_NONE,
		TYPE_COMPUTER,
		TYPE_PLAYER
	};

	BWrepPlayer();
	~BWrepPlayer();

	//
	// Query functions
	//
	const char*	getName()		const;
	int32_t		getNumber()		const;
	int32_t		getSlot()		const;
	TYPE		getType()		const;
	RACE		getRace()		const;
	char	getUnknown()	const;

	// Race of player
	bool isTerran()				const;
	bool isZerg()				const;
	bool isProtoss()			const;

	// Type of player
	bool isPlayer()				const;
	bool isComputer()			const;
	bool isEmpty()				const;

	//
	// Edit functions: for use by BWrepHeader
	//
	bool setName(const char* szName);
	bool setNumber(int32_t newNumber);
	bool setSlot(int32_t newSlot);
	bool setType(TYPE newType);
	bool setRace(RACE newRace);
	bool setUnknown(const char newUnknown);

private:
	int32_t m_number;									// 0-11
	int32_t m_slot;									// -1 if computer or none, else 0-7
	char m_type;
	char m_race;
	char m_unknown;									// normally 0, only for race 6 it is 1
	char m_name[kBWREP_PLAYERNAME_SIZE];
};

//
// rep header
//
class  BWrepHeader
{
public:
	BWrepHeader();
	~BWrepHeader();

	enum ENGINE
	{
		ENGINE_STARCRAFT=0,
		ENGINE_BROODWAR=1
	};

	//
	// Query functions
	//
	int32_t getEngine() const {return m_engine;}
	const char*	getGameName()			const;
	const char*	getGameCreatorName()	const;
	const char* getMapName()			const;
	char		getMapType()			const;
	uint16_t getMapWidth() const {return m_mapsizeW;}
	uint16_t getMapHeight() const {return m_mapsizeH;}
	time_t		getCreationDate()			const {return (time_t)m_creationDate;}

	// get player info from the playerid in BWrepAction
	bool		getPlayerFromAction(BWrepPlayer& player, int32_t playerid) const;

	// get player from index in player array (used for playerid in BWrepUnitDesc)
	bool		getPlayerFromIdx(BWrepPlayer& player, int32_t idx) const;

	// Logical queries
	int32_t		getLogicalPlayerCount()	const;
	bool		getLogicalPlayers(BWrepPlayer& player, int32_t i) const;

	//
	// Edit functions
	//
	bool setGameName(const char* szName);
	bool setGameCreatorName(const char* szName);
	bool setMapType(char cMapType);
	bool setMapName(const char* szName);

private:
    char		    m_engine;							// is 0x01 BW, 0x00 for SC
    int32_t		  m_frames;
    char		    m_fillb;							// is 0x00 everytime
    char		    m_fillc;							// is 0x00 everytime
    char		    m_filld;							// is 0x48 everytime
    int32_t     m_creationDate;
    char		    m_ka2[8];							// every byte is 0x08
    int32_t		  m_ka3;								// is 0x00 everytime
    char		    m_gamename[kBWREP_GNAME_SIZE];
	  uint16_t    m_mapsizeW;
    uint16_t    m_mapsizeH;
    char		    m_fill2[16];
    char		    m_gamecreator[kBWREP_NAME_SIZE];
    char		    m_maptype;							// 0x3C for bw reps
    char		    m_mapname[kBWREP_MAPNAME_SIZE];
    char		    m_fill3[41];
    BWrepPlayer m_oPlayer[kBWREP_NUM_PLAYERS];
    int32_t		  m_spotorder[8];						// unknown, random sequence of {0,1,..,8}
    char		  m_spot[8];							// unknown, flag if spot[n] is used
};

//----------------------------------------------------------------------------------------------------

#pragma pack(pop)

//
// user rep file access
//
class  BWrepFile
{
public:
	BWrepFile();
	virtual ~BWrepFile();

	// load replay (at least the header)
	enum {LOADMAP=1, LOADACTIONS=2, ADDACTIONS=4};
	bool Load(const char* pszFileName, int32_t options=LOADMAP|LOADACTIONS);

private:
	bool _Open(const char* pszFileName);
	bool _Close();
	bool _LoadActions(FILE *fp, bool clear);
	bool _LoadMap(FILE *fp);

	FILE*		m_pFile;

public:
	BWrepHeader m_oHeader;
	BWrepActionList m_oActions;
	BWrepMap m_oMap;
};



#endif // INC_BWREPAPI_H
