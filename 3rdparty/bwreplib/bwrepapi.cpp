#include "bwrepapi.h"
#include "bwrepgamedata.h"
#include "unpack.h"
#include <string.h>

int32_t unpack_section(FILE *file, byte *result, int size);

//
// BWrepPlayer: player information
//
BWrepPlayer::BWrepPlayer()
{
	m_number  = 0;
	m_slot    = 0;
	m_type    = TYPE_NONE;
	m_race    = RACE_ZERG;	// TODO: is this an appropriate default?
	m_unknown = 0;
	strcpy(m_name, "");
}

BWrepPlayer::~BWrepPlayer()
{
}

bool BWrepPlayer::isTerran() const
{
	return (m_race == RACE_TERRAN) ? true : false;
}

bool BWrepPlayer::isZerg() const
{
	return (m_race == RACE_ZERG) ? true : false;
}

bool BWrepPlayer::isProtoss() const
{
	return (m_race == RACE_PROTOSS) ? true : false;
}

bool BWrepPlayer::isPlayer() const
{
	return (m_type == TYPE_PLAYER) ? true : false;
}

bool BWrepPlayer::isComputer() const
{
	return (m_type == TYPE_COMPUTER) ? true : false;
}

bool BWrepPlayer::isEmpty() const
{
	return (m_type == TYPE_NONE) ? true : false;
}

const char* BWrepPlayer::getName() const
{
	return m_name;
}

int32_t BWrepPlayer::getNumber() const
{
	return m_number;
}

int32_t BWrepPlayer::getSlot() const
{
	return m_slot;
}

BWrepPlayer::TYPE BWrepPlayer::getType() const
{
	return static_cast<TYPE>(m_type);
}

BWrepPlayer::RACE BWrepPlayer::getRace() const
{
	return static_cast<RACE>(m_race);
}

char BWrepPlayer::getUnknown() const
{
	return m_unknown;
}

bool BWrepPlayer::setName(const char* szName)
{
	strcpy(m_name, szName);
	return true;
}

bool BWrepPlayer::setNumber(int32_t newNumber)
{
	if (newNumber >= 0 && newNumber <= 11)
	{
		m_number = newNumber;
	}
	else
	{
		return false;
	}
	return true;
}

bool BWrepPlayer::setSlot(int32_t newSlot)
{
	if (newSlot >= -1 && newSlot < kBWREP_NUM_SLOT)
	{
		m_slot = newSlot;
	}
	else
	{
		return false;
	}
	return true;
}

bool BWrepPlayer::setType(TYPE newType)
{
	if (newType >= TYPE_NONE && newType <= TYPE_PLAYER)
	{
		m_type = newType;
	}
	else
	{
		return false;
	}
	return true;
}

bool BWrepPlayer::setRace(RACE newRace)
{
	if (newRace >= RACE_ZERG && newRace <= RACE_6)
	{
		m_race = newRace;
	}
	else
	{
		return false;
	}
	return true;
}

bool BWrepPlayer::setUnknown(const char newUnknown)
{
	if (newUnknown == 0 || newUnknown == 1)
	{
		m_unknown = newUnknown;
	}
	else
	{
		return false;
	}
	return true;
}

//
// BWrepHeader: replay file header
//
BWrepHeader::BWrepHeader()
{
}

BWrepHeader::~BWrepHeader()
{
}

const char* BWrepHeader::getGameName() const
{
	return m_gamename;
}

const char* BWrepHeader::getGameCreatorName() const
{
	return m_gamecreator;
}

const char* BWrepHeader::getMapName() const
{
	return m_mapname;
}

char BWrepHeader::getMapType() const
{
	return m_maptype;
}

// get player from index in player array (used for playerid in BWrepUnitDesc)
bool BWrepHeader::getPlayerFromIdx(BWrepPlayer& player, int32_t idx) const
{
	if (idx >= 0 && idx < kBWREP_NUM_PLAYERS)
	{
		player=m_oPlayer[idx];
		return true;
	}
	return false;
}

bool BWrepHeader::getPlayerFromAction(BWrepPlayer& player, int32_t playerid) const
{
	if (playerid >= 0 && playerid < kBWREP_NUM_PLAYERS)
	{
		for (int32_t i = 0; i < kBWREP_NUM_PLAYERS; ++i)
		{
			if (m_oPlayer[i].getSlot()==playerid)
			{
				player = m_oPlayer[i];
				return true;
			}
		}
	}
	return false;
}

bool BWrepHeader::setGameName(const char* szName)
{
	strcpy(m_gamename, szName);
	return true;
}

bool BWrepHeader::setGameCreatorName(const char* szName)
{
	strcpy(m_gamecreator, szName);
	return true;
}

bool BWrepHeader::setMapType(char cMapType)
{
	m_maptype = cMapType;
	return true;
}

bool BWrepHeader::setMapName(const char* szName)
{
	strcpy(m_mapname, szName);
	return true;
}

int32_t BWrepHeader::getLogicalPlayerCount() const
{
	int32_t nPlayerCount = 0;
	for (int32_t i = 0; i < kBWREP_NUM_PLAYERS; ++i)
	{
		if (!m_oPlayer[i].isEmpty())
		{
			++nPlayerCount;
		}
	}
	return nPlayerCount;
}

bool BWrepHeader::getLogicalPlayers(BWrepPlayer& player, int32_t idxPlayer) const
{
	for (int32_t i = 0,j=0; i < kBWREP_NUM_PLAYERS; ++i)
	{
		if (!m_oPlayer[i].isEmpty())
		{
			if(j==idxPlayer) {player=m_oPlayer[i]; return true;}
			j++;
		}
	}
	return false;
}

//
// BWrepFile: user rep file access
//
BWrepFile::BWrepFile()
{
	m_pFile	= NULL;
}

BWrepFile::~BWrepFile()
{
	_Close();
}

bool BWrepFile::_Open(const char* pszFileName)
{
    m_pFile = fopen(pszFileName, "rb");
	return (m_pFile != NULL) ? true : false;
}

bool BWrepFile::_Close()
{
	if (m_pFile != NULL)
	{
		fclose(m_pFile);
		m_pFile=0;
	}
	return true;
}

//------------------------------------------------------------------------------------------------------------

bool BWrepFile::Load(const char* pszFileName, int32_t options)
{
	bool bOk = _Open(pszFileName);
	if(bOk)
	{
		int32_t nRepID;
		unpack_section(m_pFile, (byte*)&nRepID, sizeof(nRepID));

		bOk = (nRepID == kBWREP_ID);
		if (bOk)
		{
			// read header
			bOk = (unpack_section(m_pFile, (byte*)&m_oHeader, kBWREP_HEADER_SIZE)==0);

			// read actions
			if(bOk && (options&LOADACTIONS)!=0) bOk = _LoadActions(m_pFile,(options&ADDACTIONS)==0);

			// load map
			if(bOk && (options&LOADMAP)!=0) bOk = _LoadMap(m_pFile);
		}

		_Close();
	}
	return bOk;
}

//------------------------------------------------------------------------------------------------------------

// must be called after Load
bool BWrepFile::_LoadActions(FILE *fp, bool clear)
{
	// get section size
	int32_t cmdSize=0;
	unpack_section(fp, (byte*)&cmdSize, sizeof(cmdSize));

	// alloc buffer to read it
	byte *buffer = (byte *)malloc(cmdSize * sizeof(byte));
	if (buffer==0) return false;

	// unpack cmd section in buffer
	unpack_section(fp, buffer, cmdSize);

	// decode all actions (dont free buffer, it beint32_ts to m_oActions now)
	bool bOk = m_oActions.DecodeActions(buffer,cmdSize, clear);

	return bOk;
}

//------------------------------------------------------------------------------------------------------------

// must be called after LoadActions
bool BWrepFile::_LoadMap(FILE *fp)
{
	// get section size
	int32_t mapSize=0;
	unpack_section(fp, (byte*)&mapSize, sizeof(mapSize));

	// alloc buffer to read it
	byte *buffer = (byte *)malloc(mapSize * sizeof(byte));
	if (buffer==0) return false;

	// unpack cmd section in buffer
	unpack_section(fp, buffer, mapSize);

	// decode all actions (dont free buffer, it beint32_ts to m_oActions now)
	bool bOk = m_oMap.DecodeMap(buffer,mapSize,m_oHeader.getMapWidth(),m_oHeader.getMapHeight());

	return bOk;
}

//------------------------------------------------------------------------------------------------------------
