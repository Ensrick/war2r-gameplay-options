#include "units.h"

#include <cstring>

namespace units {

// Other spellings a player is likely to type: short hero names, and the game's own display names
// ("Grommash Hellscream", "Kurdran and Sky'ree", "Gul'dan", "Cho'gall" in Data\Strings\enUS.json).
static const struct {
    const char* alias;
    const char* name;
} kAliases[] = {
    {"uther", "uther_lightbringer"},         {"teron", "teron_gorefiend"},
    {"grom", "grom_hellscream"},             {"grommash", "grom_hellscream"},
    {"grommash_hellscream", "grom_hellscream"}, {"korgath", "korgath_bladefist"},
    {"kurdran_and_skyree", "kurdran"},       {"kurdan", "kurdran"},
    {"gul_dan", "guldan"},                   {"cho_gall", "chogall"},
    {"zul_jin", "zuljin"},                   {"gryphon", "gryphon_rider"},
    {"ogremage", "ogre_mage"},               {"deathknight", "death_knight"},
    {"sappers", "goblin_sappers"},
};

const Entry* FindByName(const char* name) {
    for (const auto& a : kAliases)
        if (_stricmp(a.alias, name) == 0) name = a.name;
    for (const Entry& e : kUnits)
        if (_stricmp(e.name, name) == 0) return &e;
    return nullptr;
}

const Entry* FindById(uint8_t id) {
    for (const Entry& e : kUnits)
        if (e.id == id) return &e;
    return nullptr;
}

Race StructureRace(int type) {
    const bool paired = (type >= 0x3A && type <= 0x5B) || (type >= 0x60 && type <= 0x63);
    if (paired) return (type & 1) ? kOrc : kHuman;
    if (type == 0x67) return kHuman;  // human wall
    if (type == 0x68) return kOrc;    // orc wall
    return kNeutral;
}

StructureGroup StructureGroupOf(int type) {
    return ((type >= 0x58 && type <= 0x5B) || (type >= 0x60 && type <= 0x63)) ? kBuildingUpgrades : kBuildings;
}

ResearchInfo ResearchInfoOf(int i) {
    // Rows 0-23 come in pairs of levels, alternating human / orc per pair.
    if (i <= 1) return {kHuman, kMeleeUpgrades};     // swords
    if (i <= 3) return {kOrc, kMeleeUpgrades};       // battle axes
    if (i <= 5) return {kHuman, kRangedUpgrades};    // arrows
    if (i <= 7) return {kOrc, kRangedUpgrades};      // throwing axes
    if (i <= 9) return {kHuman, kMeleeUpgrades};     // human shields
    if (i <= 11) return {kOrc, kMeleeUpgrades};      // orc shields
    if (i <= 13) return {kHuman, kNavalUpgrades};    // human ship cannons
    if (i <= 15) return {kOrc, kNavalUpgrades};      // orc ship cannons
    if (i <= 17) return {kHuman, kNavalUpgrades};    // human ship armor
    if (i <= 19) return {kOrc, kNavalUpgrades};      // orc ship armor
    if (i <= 21) return {kOrc, kSiegeUpgrades};      // catapult
    if (i <= 23) return {kHuman, kSiegeUpgrades};    // ballista
    if (i <= 27) return {kHuman, kRangedUpgrades};   // ranger upgrade, longbow, scouting, marksmanship
    if (i <= 31) return {kOrc, kRangedUpgrades};     // berserker upgrade, lighter axes, scouting, regeneration
    if (i == 32) return {kOrc, kKnightUpgrades};     // ogre-mage upgrade
    if (i <= 36) return {kHuman, kKnightUpgrades};   // paladin upgrade, holy vision, healing, exorcism
    if (i <= 42) return {kHuman, kSpells};           // flame shield, fireball, slow, invisibility, polymorph, blizzard
    if (i <= 44) return {kOrc, kKnightUpgrades};     // eye of kilrogg, bloodlust
    if (i <= 49) return {kOrc, kSpells};             // raise dead, death coil, whirlwind, haste, unholy armor
    if (i == 50) return {kOrc, kKnightUpgrades};     // runes
    return {kOrc, kSpells};                          // death and decay
}

}  // namespace units
