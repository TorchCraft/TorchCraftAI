#include "bwrepgamedata.h"
#include <assert.h>

const int32_t BWrepGameData::k_MaxPlayers = 12;

const char* BWrepGameData::g_Races[] = {"Zerg","Terran","Protoss"};

const char* BWrepGameData::g_Commands[] = {
  "!0x00",    //0x00
  "!0x01",
  "!0x02",
  "!0x03",
  "!0x04",
  "!0x05",    //0x05
  "!0x06",
  "!0x07",
  "!0x08",
  "Select",
  "Shift Select",    //0x0a
  "Shift Deselect",
  "Build",
  "Vision",
  "Ally",
  "!0x0f",
  "!0x10",    //0x10
  "!0x11",
  "!0x12",
  "Hotkey",
  "Move",
  "Attack",    //0x15
  "!0x16",
  "!0x17",
  "Cancel",
  "Cancel Hatch",
  "Stop",    //0x1a
  "!0x1b",
  "!0x1c",
  "!0x1d",
  "Return Cargo",
  "Train",
  "Cancel Train",    //cancel train
  "Cloak",    //cloak?
  "Decloak",    //decloak?
  "Hatch",
  "!0x24",
  "Unsiege",    //0x25
  "Siege",
  "Arm (Interceptor/Scarab)",
  "Unload All",
  "Unload",
  "Merge Archon",    //0x2a
  "Hold Position",
  "Burrow",
  "Unburrow",
  "Cancel Nuke",
  "Lift",
  "Research",    //0x30
  "!0x31",
  "Upgrade",
  "!0x33",
  "!0x34",
  "Morph",    //0x35
  "Stim",
  "!0x37",
  "!0x38",
  "!0x39",
  "!0x3a",    //0x3a
  "!0x3b",
  "!0x3c",
  "!0x3d",
  "!0x3e",
  "!0x3f",
  "!0x40",    //0x40
  "!0x41",
  "!0x42",
  "!0x43",
  "!0x44",
  "!0x45",    //0x45
  "!0x46",
  "!0x47",
  "!0x48",
  "!0x49",
  "!0x4a",    //0x4a
  "!0x4b",
  "!0x4c",
  "!0x4d",
  "!0x4e",
  "!0x4f",
  "!0x50",    //0x50
  "!0x51",
  "!0x52",
  "!0x53",
  "!0x54",
  "!0x55",
  "!0x56",
  "Leave Game",
  "Minimap Ping",
  "!0x59",
  "Merge Dark Archon",
  "Deselect",
  "Chat"
};
const int32_t BWrepGameData::g_CommandsSize = sizeof(g_Commands)/sizeof(g_Commands[0]);

const char* BWrepGameData::g_Research[] = {
	"Stim Pack",	//0x00
	"Lockdown",
	"EMP Shockwave",
	"Spider Mines",
	"",
	"Siege Tank", //0x05
	"",
	"Irradiate",
	"Yamato Gun",
	"Cloaking Field", //wraiths
	"Personal Cloaking", //0x0a
	"Burrow",
	"",
	"Spawn Broodling",
	"",
	"Plague",
	"Consume",	//0x10
	"Ensnare",
	"",
	"Psionic Storm",
	"Hallucination",
	"Recall", //0x15
	"Stasis Field",
	"",
	"Restoration",
	"Disruption Web",
	"", //0x1a
	"Mind Control",
	"",
	"",
	"Optical Flare",
	"Maelstrom",
	"Lurker Aspect",	//0x20
	"",
	"",
	"",
	"",
	"", //0x25
	"",
	"",
	"",
	"",
	"", //0x2a
	"",
	"",
	"",
	"",
	"",
	"", //0x30
	"",
};
const int32_t BWrepGameData::g_ResearchSize = sizeof(g_Research)/sizeof(g_Research[0]);

const char* BWrepGameData::g_Upgrades[] = {
	"Terran Infantry Armor",	//0x00
	"Terran Vehicle Plating",
	"Terran Ship Plating",
	"Zerg Carapace",
	"Zerg Flyer Carapace",
	"Protoss Ground Armor", //0x05
	"Protoss Air Armor",
	"Terran Infantry Weapons",
	"Terran Vehicle Weapons",
	"Terran Ship Weapons",
	"Zerg Melee Attacks", //0x0a
	"Zerg Missile Attacks",
	"Zerg Flyer Attacks",
	"Protoss Ground Weapons",
	"Protoss Air Weapons",
	"Protoss Plasma Shields",
	"U-238 Shells (Marine Range)",	//0x10
	"Ion Thrusters (Vulture Speed)",
	"",
	"Titan Reactor (Science Vessel Energy)",
	"Ocular Implants (Ghost Sight)",
	"Moebius Reactor (Ghost Energy)", //0x15
	"Apollo Reactor (Wraith Energy)",
	"Colossus Reactor (Battle Cruiser Energy)",
	"Ventral Sacs (Overlord Transport)",
	"Antennae (Overlord Sight)",
	"Pneumatized Carapace (Overlord Speed)", //0x1a
	"Metabolic Boost (Zergling Speed)",
	"Adrenal Glands (Zergling Attack)",
	"Muscular Augments (Hydralisk Speed)",
	"Grooved Spines (Hydralisk Range)",
	"Gamete Meiosis (Queen Energy)",
	"Defiler Energy",	//0x20
	"Singularity Charge (Dragoon Range)",
	"Leg Enhancement (Zealot Speed)",
	"Scarab Damage",
	"Reaver Capacity",
	"Gravitic Drive (Shuttle Speed)", //0x25
	"Sensor Array (Observer Sight)",
	"Gravitic Booster (Observer Speed)",
	"Khaydarin Amulet (Templar Energy)",
	"Apial Sensors (Scout Sight)",
	"Gravitic Thrusters (Scout Speed)", //0x2a
	"Carrier Capacity",
	"Khaydarin Core (Arbiter Energy)",
	"",
	"",
	"Argus Jewel (Corsair Energy)",
	"", //0x30
	"Argus Talisman (Dark Templar Energy)",
	"",
	"Caduceus Reactor (Medic Energy)",
	"Chitinous Plating (Ultralisk Armor)",
	"Anabolic Synthesis (Ultralisk Speed)", //0x35
	"Charon Boosters (Goliath Range)",
	"",
	"",
	"",
	"", //0x3a
};

const int32_t BWrepGameData::g_UpgradesSize = sizeof(g_Upgrades)/sizeof(g_Upgrades[0]);

const char* BWrepGameData::g_BuildingTypes[] = {
	"",	//0x00
	"",
	"",
	"",
	"",
	"", //0x05
	"",
	"",
	"",
	"",
	"", //0x0a
	"",
	"",
	"",
	"",
	"",
	"",	//0x10
	"",
	"",
	"",
	"",
	"", //0x15
	"",
	"",
	"",
	"Morph", //"Zerg Building",
	"", //0x1a
	"",
	"",
	"",
	"Build", //"Terran Building",
	"Warp", //"Protoss Building",
	"",	//0x20
	"",
	"",
	"",
	"Add-On", //"Terran Add-On",
	"", //0x25
	"",
	"",
	"",
	"",
	"", //0x2a
	"",
	"",
	"",
	"Evolve", //"Zerg Add-On",
	"",
	"",	//0x30
	"",
	"",
	"",
	"",
	"", //0x35
	"",
	"",
	"",
	"",
	"", //0x3a
	"",
	"",
	"",
	"",
	"",
	"",	//0x40
	"",
	"",
	"",
	"",
	"", //0x45
	"",
	"Land",	//terran landing building
	"",
	"",
	"", //0x4a
	"",
	"",
	"",
	"",
	"",
};

const int32_t BWrepGameData::g_BuildingTypesSize = sizeof(g_BuildingTypes)/sizeof(g_BuildingTypes[0]);

const char* BWrepGameData::g_Objects[] = {
	"Marine",	//0x00
	"Ghost",
	"Vulture",
	"Goliath",
	"",
	"Siege Tank", //0x05
	"",
	"SCV",
	"Wraith",
	"Science Vessel",
	"", //0x0a
	"Dropship",
	"Battlecruiser",
	"",
	"Nuke",
	"",
	"",	//0x10
	"",
	"",
	"",
	"",
	"", //0x15
	"",
	"",
	"",
	"",
	"", //0x1a
	"",
	"",
	"",
	"",
	"",
	"Firebat",	//0x20
	"",
	"Medic",
	"",
	"",
	"Zergling", //0x25
	"Hydralisk",
	"Ultralisk",
	"",
	"Drone",
	"Overlord", //0x2a
	"Mutalisk",
	"Guardian",
	"Queen",
	"Defiler",
	"Scourge",
	"",	//0x30
	"",
	"Infested Terran",
	"",
	"",
	"", //0x35
	"",
	"",
	"",
	"",
	"Valkyrie", //0x3a
	"",
	"Corsair",
	"Dark Templar",
	"Devourer",
	"",
	"Probe",	//0x40
	"Zealot",
	"Dragoon",
	"High Templar",
	"",
	"Shuttle", //0x45
	"Scout",
	"Arbiter",
	"Carrier",
	"",
	"", //0x4a
	"",
	"",
	"",
	"",
	"",
	"",	//0x50
	"",
	"",
	"Reaver",
	"Observer",
	"", //0x55
	"",
	"",
	"",
	"",
	"", //0x5a
	"",
	"",
	"",
	"",
	"",
	"",	//0x60
	"",
	"",
	"",
	"",
	"", //0x65
	"",
	"Lurker",
	"",
	"",
	"Command Center",	//0x006a
	"ComSat",
	"Nuclear Silo",
	"Supply Depot",
	"Refinery",	//refinery?
	"Barracks",
	"Academy",	//Academy?	//0x0070
	"Factory",
	"Starport",
	"Control Tower",
	"Science Facility",
	"Covert Ops",	//0x0075
	"Physics Lab",
	"",
	"Machine Shop",
	"",
	"Engineering Bay",	//0x007a
	"Armory",
	"Missile Turret",
	"Bunker",
	"",
	"",
	"",	//0x80
	"",
	"",
	"Hatchery",
	"Lair",
	"Hive", //0x85
	"Nydus Canal",
	"Hydralisk Den",
	"Defiler Mound",
	"Greater Spire",
	"Queens Nest", //0x8a
	"Evolution Chamber",
	"Ultralisk Cavern",
	"Spire",
	"Spawning Pool",
	"Creep Colony",
	"Spore Colony",	//0x90
	"",
	"Sunken Colony",
	"",
	"",
	"Extractor", //0x95
	"",
	"",
	"",
	"",
	"Nexus", //0x9a
	"Robotics Facility",
	"Pylon",
	"Assimilator",
	"",
	"Observatory",
	"Gateway",	//0xa0
	"",
	"Photon Cannon",
	"Citadel of Adun",
	"Cybernetics Core",
	"Templar Archives", //0xa5
	"Forge",
	"Stargate",
	"",
	"Fleet Beacon",
	"Arbiter Tribunal", //0xaa
	"Robotics Support Bay",
	"Shield Battery",
	"",
	"",
	"",
	"",	//0xb0
	"",
	"",
	"",
	"",
	"", //0xb5
	"",
	"",
	"",
	"",
	"", //0xba
	"",
	"",
	"",
	"",
	"",
};

const int32_t BWrepGameData::g_ObjectsSize = sizeof(g_Objects)/sizeof(g_Objects[0]);

const char* BWrepGameData::g_Attacks[] = {
	"Move",	//0x00 move with right click
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x05
	"Move", // move with key or click on icon (can be attack icon as well)
	"Unknown",
	"Attack",
	"Unknown",
	"Unknown", //0x0a
	"Unknown",
	"Unknown",
	"Unknown",
	"Attack Move",
	"Unknown",
	"Unknown",	//0x10
	"Unknown",
	"Unknown",
	"Failed Casting?",
	"Unknown",
	"Unknown", //0x15
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x1a
	"Infest CC",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",	//0x20
	"Unknown",
	"Repair",
	"Unknown",
	"Unknown",
	"Unknown", //0x25
	"Unknown",
	"Clear Rally",
	"Set Rally",
	"Unknown",
	"Unknown", //0x2a
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",	//0x30
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x35
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x3a
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",	//0x40
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x45
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x4a
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Gather",
	"Unknown",	//0x50
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x55
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x5a
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",	//0x60
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x65
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",	//0x006a
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unload",	//0x0070
	"Yamato",
	"Unknown",
	"Lockdown",
	"Unknown",
	"Unknown",	//0x0075
	"Unknown",
	"Dark Swarm",
	"Parasite",
	"Spawn Broodling",
	"EMP",	//0x007a
	"Unknown",
	"Unknown",
	"Unknown",
	"Launch Nuke",
	"Unknown",
	"Unknown",	//0x80
	"Unknown",
	"Unknown",
	"Unknown",
	"Lay Mine",
	"Unknown", //0x85
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0x8a
	"ComSat Scan",
	"Unknown",
	"Defense Matrix",
	"Unknown",
	"Recall", // & lockdown
	"Plague",	//0x90
	"Consume",
	"Ensnare",
	"Stasis",
	"Unknown",
	"Unknown", //0x95
	"Unknown",
	"Unknown",
	"Patrol",
	"Unknown",
	"Unknown", //0x9a
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",	//0xa0
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0xa5
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown", //0xaa
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",	//0xb0
	"Heal",
	"Unknown",
	"Unknown",
	"Restore",
	"Disruption Web", //0xb5
	"Mind Control",
	"Unknown",
	"Feedback",
	"Optic Flare",
	"Maelstrom", //0xba
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Irradiate"
};

const int32_t BWrepGameData::g_AttacksSize = sizeof(g_Attacks)/sizeof(g_Attacks[0]);

const char* BWrepGameData::g_AttackModifiers[] = {
	"",
	"Shift-Queue",
};

const int32_t BWrepGameData::g_AttackModifiersSize = sizeof(g_AttackModifiers)/sizeof(g_AttackModifiers[0]);

const char* BWrepGameData::g_HotKeyModifiers[] = {
	"Assign",
	"Select",
  "Add",
};

const int32_t BWrepGameData::g_HotKeyModifiersSize = sizeof(g_HotKeyModifiers)/sizeof(g_HotKeyModifiers[0]);

//------------------------------------------------------------------------------------------------------------

// get action name from action id
const char *BWrepGameData::GetActionNameFromID(int32_t id)
{
	assert(id>=0 && id<BWrepGameData::g_CommandsSize);
  return BWrepGameData::g_Commands[id];
}

//------------------------------------------------------------------------------------------------------------

// get object name from object id
const char *BWrepGameData::GetObjectNameFromID(int32_t id)
{
	assert(id>=0 && id<BWrepGameData::g_ObjectsSize);
	return BWrepGameData::g_Objects[id];
}

//------------------------------------------------------------------------------------------------------------

// get research name from research id
const char *BWrepGameData::GetResearchNameFromID(int32_t id)
{
	assert(id>=0 && id<BWrepGameData::g_ResearchSize);
	return BWrepGameData::g_Research[id];
}

//------------------------------------------------------------------------------------------------------------

// get upgrade name from upgrade id
const char *BWrepGameData::GetUpgradeNameFromID(int32_t id)
{
	static char buffer[128];
	assert(id>=0 && id<BWrepGameData::g_UpgradesSize);
	strcpy(buffer,BWrepGameData::g_Upgrades[id]);
	char *p=strchr(buffer,'('); if(p!=0) p=strtok(p+1,")"); else p=buffer;
	return p;
}

//------------------------------------------------------------------------------------------------------------

// get attack name from attack id
const char *BWrepGameData::GetAttackNameFromID(int32_t id)
{
	assert(id>=0 && id<BWrepGameData::g_AttacksSize);
	return BWrepGameData::g_Attacks[id];
}

//------------------------------------------------------------------------------------------------------------
