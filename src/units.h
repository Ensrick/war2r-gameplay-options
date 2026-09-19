// Unit type ids (PUD numbering), the names the config file uses for them, and the race / group every unit,
// structure and research item belongs to for the [health] [costs] [time] multipliers.
// Ids 0x00-0x39 checked against Data\Rez\unitdata.dat flags/HP; structure ids 0x3A-0x68 against its prices and the
// hall / tower / mill flags; research indices are PUD UGRD order (docs/research/data_tables.md section 2).
#pragma once
#include <cstdint>

namespace units {

constexpr int kFirstBuilding = 0x3A;  // farm; everything below is a mobile unit (or an unused slot)
constexpr int kTypeCount = 110;
constexpr int kResearchCount = 52;

enum Race : uint8_t { kHuman, kOrc, kNeutral, kRaceCount };

// Same group names in every multiplier table of the config.
enum UnitGroup : uint8_t {
    kWorkers,     // peasant, peon (and their "attack" variants)
    kMelee,       // footman, knight, paladin / grunt, ogre, ogre-mage
    kRanged,      // archer, ranger / axethrower, berserker
    kSiege,       // ballista / catapult
    kCasters,     // mage / death knight
    kAir,         // gryphon rider, flying machine / dragon, zeppelin
    kNaval,       // every ship
    kDemolition,  // dwarves / goblin sappers
    kHeroes,      // whatever [heroes] units lists (health only: heroes are never trained)
    kOther,       // skeleton, daemon, critter, eye of kilrogg: only the neutral "all" applies
    kUnitGroupCount
};

enum StructureGroup : uint8_t { kBuildings, kBuildingUpgrades, kStructureGroupCount };

enum ResearchGroup : uint8_t {
    kMeleeUpgrades,   // swords, shields / battle axes, shields
    kRangedUpgrades,  // arrows + ranger line / throwing axes + berserker line
    kSiegeUpgrades,   // ballista / catapult
    kNavalUpgrades,   // ship cannons, ship armor
    kKnightUpgrades,  // paladin upgrade + holy vision, healing, exorcism / ogre-mage upgrade + eye, bloodlust, runes
    kSpells,          // mage spells / death knight spells
    kResearchGroupCount
};

struct Entry {
    uint8_t id;
    const char* name;
    bool hero;
    Race race;
    UnitGroup group;
};

constexpr Entry kUnits[] = {
    {0x00, "footman", false, kHuman, kMelee},           {0x01, "grunt", false, kOrc, kMelee},
    {0x02, "peasant", false, kHuman, kWorkers},         {0x03, "peon", false, kOrc, kWorkers},
    {0x04, "ballista", false, kHuman, kSiege},          {0x05, "catapult", false, kOrc, kSiege},
    {0x06, "knight", false, kHuman, kMelee},            {0x07, "ogre", false, kOrc, kMelee},
    {0x08, "archer", false, kHuman, kRanged},           {0x09, "axethrower", false, kOrc, kRanged},
    {0x0A, "mage", false, kHuman, kCasters},            {0x0B, "death_knight", false, kOrc, kCasters},
    {0x0C, "paladin", false, kHuman, kMelee},           {0x0D, "ogre_mage", false, kOrc, kMelee},
    {0x0E, "dwarves", false, kHuman, kDemolition},      {0x0F, "goblin_sappers", false, kOrc, kDemolition},
    {0x10, "attack_peasant", false, kHuman, kWorkers},  {0x11, "attack_peon", false, kOrc, kWorkers},
    {0x12, "ranger", false, kHuman, kRanged},           {0x13, "berserker", false, kOrc, kRanged},
    {0x14, "alleria", true, kHuman, kHeroes},           {0x15, "teron_gorefiend", true, kOrc, kHeroes},
    {0x16, "kurdran", true, kHuman, kHeroes},           {0x17, "dentarg", true, kOrc, kHeroes},
    {0x18, "khadgar", true, kHuman, kHeroes},           {0x19, "grom_hellscream", true, kOrc, kHeroes},
    {0x1A, "human_tanker", false, kHuman, kNaval},      {0x1B, "orc_tanker", false, kOrc, kNaval},
    {0x1C, "human_transport", false, kHuman, kNaval},   {0x1D, "orc_transport", false, kOrc, kNaval},
    {0x1E, "elven_destroyer", false, kHuman, kNaval},   {0x1F, "troll_destroyer", false, kOrc, kNaval},
    {0x20, "battleship", false, kHuman, kNaval},        {0x21, "juggernaught", false, kOrc, kNaval},
    {0x23, "deathwing", true, kOrc, kHeroes},           {0x26, "gnomish_submarine", false, kHuman, kNaval},
    {0x27, "giant_turtle", false, kOrc, kNaval},        {0x28, "flying_machine", false, kHuman, kAir},
    {0x29, "zeppelin", false, kOrc, kAir},              {0x2A, "gryphon_rider", false, kHuman, kAir},
    {0x2B, "dragon", false, kOrc, kAir},                {0x2C, "turalyon", true, kHuman, kHeroes},
    {0x2D, "eye_of_kilrogg", false, kNeutral, kOther},  {0x2E, "danath", true, kHuman, kHeroes},
    {0x2F, "korgath_bladefist", true, kOrc, kHeroes},   {0x31, "chogall", true, kOrc, kHeroes},
    {0x32, "lothar", true, kHuman, kHeroes},            {0x33, "guldan", true, kOrc, kHeroes},
    {0x34, "uther_lightbringer", true, kHuman, kHeroes}, {0x35, "zuljin", true, kOrc, kHeroes},
    {0x37, "skeleton", false, kNeutral, kOther},        {0x38, "daemon", false, kNeutral, kOther},
    {0x39, "critter", false, kNeutral, kOther},
    // Structures that shoot, so a [unit.<name>] table can set their range and damage. Ids >= kFirstBuilding are ignored by everything unit-only.
    {0x60, "human_guard_tower", false, kHuman, kOther},  {0x61, "orc_guard_tower", false, kOrc, kOther},
    {0x62, "human_cannon_tower", false, kHuman, kOther}, {0x63, "orc_cannon_tower", false, kOrc, kOther},
};

const Entry* FindByName(const char* name);  // case-insensitive, nullptr when unknown
const Entry* FindById(uint8_t id);

// Structures: 0x3A-0x5B alternate human / orc (farm, pig farm, ... castle, fortress); towers and walls likewise.
// Gold mine, oil patch, start locations, circle of power, dark portal and runestone are neutral.
Race StructureRace(int type);
// A structure upgrade is priced and timed as the type it turns into: keep / stronghold, castle / fortress,
// guard and cannon towers.
StructureGroup StructureGroupOf(int type);

struct ResearchInfo {
    Race race;
    ResearchGroup group;
};
ResearchInfo ResearchInfoOf(int index);  // index into the UGRD tables, 0..51

}  // namespace units
