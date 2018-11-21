//----------------------------------------------------------------------------------------------------
// BW Game Data - fractaled+WilliamWC3+jca (May 2003)
//----------------------------------------------------------------------------------------------------
// Only the eACTIONNAME and eHOTKEY should be int32_teresting here
//----------------------------------------------------------------------------------------------------
#ifndef __BWREPGAMEDATA_H
#define __BWREPGAMEDATA_H
#include <string.h>
#include <inttypes.h>

class  BWrepGameData
{
public:
	static const int32_t k_MaxPlayers;
	static const char* g_Races[];

	static const char* g_Commands[];
	static const int32_t g_CommandsSize;

	static const char* g_Research[];
	static const int32_t g_ResearchSize;

	static const char* g_Upgrades[];
	static const int32_t g_UpgradesSize;

	static const char* g_BuildingTypes[];
	static const int32_t g_BuildingTypesSize ;

	static const char* g_Objects[];
	static const int32_t g_ObjectsSize;

	static const char* g_Attacks[];
	static const int32_t g_AttacksSize ;

	static const char* g_AttackModifiers[];
	static const int32_t g_AttackModifiersSize;

	static const char* g_HotKeyModifiers[];
	static const int32_t g_HotKeyModifiersSize;

	// get action name from action id (action id is from eACTIONNAME)
	static const char *GetActionNameFromID(int32_t id);

	// get object name from object id (object id is from eOBJECTNAME)
	static const char *GetObjectNameFromID(int32_t id);

	// get research name from research id
	static const char *GetResearchNameFromID(int32_t id);

	// get upgrade name from upgrade id
	static const char *GetUpgradeNameFromID(int32_t id);

	// get attack name from attack id
	static const char *GetAttackNameFromID(int32_t id);

	// all actions values
	typedef enum {
		CMD_0X00,	//0X00
		CMD_0X01,
		CMD_0X02,
		CMD_0X03,
		CMD_0X04,
		CMD_0X05,	//0X05
		CMD_0X06,
		CMD_0X07,
		CMD_0X08,
		CMD_SELECT,
		CMD_SHIFTSELECT,	//0X0A
		CMD_SHIFTDESELECT,
		CMD_BUILD,
		CMD_VISION,
		CMD_ALLY,
		CMD_0X0F,
		CMD_0X10,	//0X10
		CMD_0X11,
		CMD_0X12,
		CMD_HOTKEY,
		CMD_MOVE,
		CMD_ATTACK,	//0X15
		CMD_0X16,
		CMD_0X17,
		CMD_CANCEL,
		CMD_CANCELHATCH,
		CMD_STOP,	//0X1A
		CMD_0X1B,
		CMD_0X1C,
		CMD_0X1D,
		CMD_RETURNCARGO,
		CMD_TRAIN,
		CMD_CANCELTRAIN,	//CANCEL TRAIN
		CMD_CLOAK,	//CLOAK?
		CMD_DECLOAK,	//DECLOAK?
		CMD_HATCH,
		CMD_0X24,
		CMD_UNSIEGE,	//0X25
		CMD_SIEGE,
		CMD_ARM,
		CMD_UNLOADALL,
		CMD_UNLOAD,
		CMD_MERGEARCHON,	//0X2A
		CMD_HOLDPOSITION,
		CMD_BURROW,
		CMD_UNBURROW,
		CMD_CANCELNUKE,
		CMD_LIFT,
		CMD_RESEARCH,	//0X30
		CMD_0X31,
		CMD_UPGRADE,
		CMD_0X33,
		CMD_0X34,
		CMD_MORPH,	//0X35
		CMD_STIM,
		CMD_0X37,
		CMD_0X38,
		CMD_0X39,
		CMD_0X3A,	//0X3A
		CMD_0X3B,
		CMD_0X3C,
		CMD_0X3D,
		CMD_0X3E,
		CMD_0X3F,
		CMD_0X40,	//0X40
		CMD_0X41,
		CMD_0X42,
		CMD_0X43,
		CMD_0X44,
		CMD_0X45,	//0X45
		CMD_0X46,
		CMD_0X47,
		CMD_0X48,
		CMD_0X49,
		CMD_0X4A,	//0X4A
		CMD_0X4B,
		CMD_0X4C,
		CMD_0X4D,
		CMD_0X4E,
		CMD_0X4F,
		CMD_0X50,	//0X50
		CMD_0X51,
		CMD_0X52,
		CMD_0X53,
		CMD_0X54,
		CMD_0X55,
		CMD_0X56,
		CMD_LEAVEGAME,
		CMD_0X58,
		CMD_0X59,
		CMD_MERGEDARKARCHON,
		_CMD_MAX_
	} eACTIONNAME;


	// all objects values
	typedef enum {
		OBJ_MARINE,	//0X00
		OBJ_GHOST,
		OBJ_VULTURE,
		OBJ_GOLIATH,
		OBJ_0X04,
		OBJ_SIEGETANK, //0X05
		OBJ_0X06,
		OBJ_SCV,
		OBJ_WRAITH,
		OBJ_SCIENCEVESSEL,
		OBJ_0X0A, //0X0A
		OBJ_DROPSHIP,
		OBJ_BATTLECRUISER,
		OBJ_0X0D,
		OBJ_NUKE,
		OBJ_0X0F,
		OBJ_0X10,	//0X10
		OBJ_0X11,
		OBJ_0X12,
		OBJ_0X13,
		OBJ_0X14,
		OBJ_0X15, //0X15
		OBJ_0X16,
		OBJ_0X17,
		OBJ_0X18,
		OBJ_0X19,
		OBJ_0X1A, //0X1A
		OBJ_0X1B,
		OBJ_0X1C,
		OBJ_0X1D,
		OBJ_0X1E,
		OBJ_0X1F,
		OBJ_FIREBAT,	//0X20
		OBJ_0X21,
		OBJ_MEDIC,
		OBJ_0X23,
		OBJ_0X24,
		OBJ_ZERGLING, //0X25
		OBJ_HYDRALISK,
		OBJ_ULTRALISK,
		OBJ_0X28,
		OBJ_DRONE,
		OBJ_OVERLORD, //0X2A
		OBJ_MUTALISK,
		OBJ_GUARDIAN,
		OBJ_QUEEN,
		OBJ_DEFILER,
		OBJ_SCOURGE,
		OBJ_0X30,	//0X30
		OBJ_0X31,
		OBJ_INFESTEDTERRAN,
		OBJ_0X33,
		OBJ_0X34,
		OBJ_0X35, //0X35
		OBJ_0X36,
		OBJ_0X37,
		OBJ_0X38,
		OBJ_0X39,
		OBJ_VALKYRIE, //0X3A
		OBJ_0X3B,
		OBJ_CORSAIR,
		OBJ_DARKTEMPLAR,
		OBJ_DEVOURER,
		OBJ_0X3F,
		OBJ_PROBE,	//0X40
		OBJ_ZEALOT,
		OBJ_DRAGOON,
		OBJ_HIGHTEMPLAR,
		OBJ_0X44,
		OBJ_SHUTTLE, //0X45
		OBJ_SCOUT,
		OBJ_ARBITER,
		OBJ_CARRIER,
		OBJ_0X49,
		OBJ_0X4A, //0X4A
		OBJ_0X4B,
		OBJ_0X4C,
		OBJ_0X4D,
		OBJ_0X4E,
		OBJ_0X4F,
		OBJ_0X50,	//0X50
		OBJ_0X51,
		OBJ_0X52,
		OBJ_REAVER,
		OBJ_OBSERVER,
		OBJ_0X55, //0X55
		OBJ_0X56,
		OBJ_0X57,
		OBJ_0X58,
		OBJ_0X59,
		OBJ_0X5A, //0X5A
		OBJ_0X5B,
		OBJ_0X5C,
		OBJ_0X5D,
		OBJ_0X5E,
		OBJ_0X5F,
		OBJ_0X60,	//0X60
		OBJ_0X61,
		OBJ_0X62,
		OBJ_0X63,
		OBJ_0X64,
		OBJ_0X65, //0X65
		OBJ_0X66,
		OBJ_LURKER,
		OBJ_0X68,
		OBJ_0X69,
		OBJ_COMMANDCENTER,	//0X006A
		OBJ_COMSAT,
		OBJ_NUCLEARSILO,
		OBJ_SUPPLYDEPOT,
		OBJ_REFINERY,	//REFINERY?
		OBJ_BARRACKS,
		OBJ_ACADEMY,	//ACADEMY?	//0X0070
		OBJ_FACTORY,
		OBJ_STARPORT,
		OBJ_CONTROLTOWER,
		OBJ_SCIENCEFACILITY,
		OBJ_COVERTOPS,	//0X0075
		OBJ_PHYSICSLAB,
		OBJ_0X77,
		OBJ_MACHINESHOP,
		OBJ_0X79,
		OBJ_ENGINEERINGBAY,	//0X007A
		OBJ_ARMORY,
		OBJ_MISSILETURRET,
		OBJ_BUNKER,
		OBJ_0X7E,
		OBJ_0X7F,
		OBJ_0X80,	//0X80
		OBJ_0X81,
		OBJ_0X82,
		OBJ_HATCHERY,
		OBJ_LAIR,
		OBJ_HIVE, //0X85
		OBJ_NYDUSCANAL,
		OBJ_HYDRALISKDEN,
		OBJ_DEFILERMOUND,
		OBJ_GREATERSPIRE,
		OBJ_QUEENSNEST, //0X8A
		OBJ_EVOLUTIONCHAMBER,
		OBJ_ULTRALISKCAVERN,
		OBJ_SPIRE,
		OBJ_SPAWNINGPOOL,
		OBJ_CREEPCOLONY,
		OBJ_SPORECOLONY,	//0X90
		OBJ_0X91,
		OBJ_SUNKENCOLONY,
		OBJ_0X93,
		OBJ_0X94,
		OBJ_EXTRACTOR, //0X95
		OBJ_0X96,
		OBJ_0X97,
		OBJ_0X98,
		OBJ_0X99,
		OBJ_NEXUS, //0X9A
		OBJ_ROBOTICSFACILITY,
		OBJ_PYLON,
		OBJ_ASSIMILATOR,
		OBJ_0X9E,
		OBJ_OBSERVATORY,
		OBJ_GATEWAY,	//0XA0
		OBJ_0XA1,
		OBJ_PHOTONCANNON,
		OBJ_CITADELOFADUN,
		OBJ_CYBERNETICSCORE,
		OBJ_TEMPLARARCHIVES, //0XA5
		OBJ_FORGE,
		OBJ_STARGATE,
		OBJ_0XA8,
		OBJ_FLEETBEACON,
		OBJ_ARBITERTRIBUNAL, //0XAA
		OBJ_ROBOTICSSUPPORTBAY,
		OBJ_SHIELDBATTERY,
		OBJ_0XAD,
		OBJ_0XAE,
		OBJ_0XAF,
		OBJ_0XB0,	//0XB0
		OBJ_0XB1,
		OBJ_0XB2,
		OBJ_0XB3,
		OBJ_0XB4,
		OBJ_0XB5, //0XB5
		OBJ_0XB6,
		OBJ_0XB7,
		OBJ_0XB8,
		OBJ_0XB9,
		OBJ_0XBA, //0XBA
		OBJ_0XBB,
		OBJ_0XBC,
		OBJ_0XBD,
		OBJ_0XBE,
		OBJ_0XBF,
		_OBJ_MAX_
	} eOBJECTNAME;

	// hot keys
	typedef enum
	{
		HOT_ASSIGN,
		HOT_SELECT
	} eHOTKEY;

	// building action types
	typedef enum {
		BTYP_MORPH=0x19,
		BTYP_BUILD=0x1E, //"Terran Building",
		BTYP_WARP=0x1F, //"Protoss Building",
		BTYP_ADDON=0x24, //"Terran Add-On",
		BTYP_EVOLVE=0x2E, //"Zerg Add-On",
		BTYP_LAND=0x47	//terran landing building
	} eBUILDINGACTIONTYPE;

	// attacks type
	typedef enum {
		ATT_MOVECLICK=0x00,	//0X00 MOVE WITH RIGHT CLICK
		ATT_MOVE=0x06, // MOVE WITH KEY OR CLICK ON ICON (CAN BE ATTACK ICON AS WELL)
		ATT_ATTACK=0x08,
		ATT_ATTACKMOVE=0x0E,
		ATT_FAILEDCASTING=0x13,
		ATT_INFESTCC=0x1B,
		ATT_REPAIR=0x22,
		ATT_CLEARRALLY=0x27,
		ATT_SETRALLY=0x28,
		ATT_GATHER=0x4F,
		ATT_UNLOAD=0x70,	//0X0070
		ATT_YAMATO=0x71,
		ATT_LOCKDOWN=0x73,
		ATT_DARKSWARM=0x77,
		ATT_PARASITE=0x78,
		ATT_SPAWNBROODLING=0x79,
		ATT_EMP=0x7A,	//0X007A
		ATT_LAUNCHNUKE=0x7E,
		ATT_LAYMINE=0x84,
		ATT_COMSATSCAN=0x8B,
		ATT_DEFENSEMATRIX=0x8D,
		ATT_RECALL=0x8F, // & LOCKDOWN
		ATT_PLAGUE=0x90,	//0X90
		ATT_CONSUME=0x91,
		ATT_ENSNARE=0x92,
		ATT_STASIS=0x93,
		ATT_PATROL=0x98,
		ATT_HEAL=0xB1,
		ATT_RESTORE=0xB4,
		ATT_DISRUPTIONWEB=0xB5, //0XB5
		ATT_MINDCONTROL=0xB6,
		ATT_FEEDBACK=0xB8,
		ATT_OPTICFLARE=0xB9,
		ATT_MAELSTROM=0xBA, //0XBA
		ATT_UNKNOWN=0xBF,
		ATT_IRRADIATE=0xC0,
		_ATT_MAX_
	} eATTACKTYPE;
};

#endif


