#include "config.h"

#include <windows.h>
#include <cstdio>
#include <string>

#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#include "../third_party/tomlplusplus/toml.hpp"

#include "log.h"
#include "spells.h"
#include "units.h"

namespace config {

Config g;
const char* const kSpellKeys[kSpellCount] = {"heal",         "exorcism",     "slow",     "polymorph",    "bloodlust",
                                             "death_coil",   "haste",        "unholy_armor", "raise_dead", "holy_vision",
                                             "flame_shield", "fireball",     "invisibility", "blizzard", "death_and_decay",
                                             "whirlwind",    "runes"};

const char* const kStatKeys[kStatCount] = {"hit_points", "armor", "basic_damage", "piercing_damage", "range",
                                           "sight",      "gold",  "lumber",       "oil",             "build_time"};

const char* const kSpellCostKeys[kSpellCostCount] = {
    "holy_vision", "heal", "exorcism", "flame_shield", "fireball", "slow", "invisibility", "polymorph", "blizzard",
    "eye_of_kilrogg", "bloodlust", "raise_dead", "death_coil", "whirlwind", "haste", "unholy_armor", "runes", "death_and_decay"};
const char* const kProductionClassKeys[kProdClassCount] = {"workers",     "infantry",    "archers",    "knights",
                                                           "casters",     "flyers",      "siege",      "tankers",
                                                           "destroyers",  "battleships", "submarines"};
const char* const kSpellDamageKeys[kSpellDamageCount] = {"fireball",  "flame_shield", "blizzard", "death_and_decay",
                                                         "whirlwind", "death_coil",   "runes",    "heal"};

static wchar_t g_path[MAX_PATH];
static FILETIME g_mtime;

// Default Polymorph priority: flying attackers, then casters, then the big ground units (max HP 90+).
static const char* const kDefaultPolymorphTargets[] = {
    "deathwing", "kurdran", "dragon", "gryphon_rider", "daemon",
    "death_knight", "mage", "ogre_mage", "paladin",
    "teron_gorefiend", "guldan", "khadgar", "dentarg", "chogall", "turalyon", "uther_lightbringer",
    "grom_hellscream", "korgath_bladefist", "danath", "alleria", "knight", "ogre", "lothar",
};

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void ReadBool(const toml::table& root, const char* section, const char* key, bool& out) {
    const auto node = root[section][key];
    if (!node) return;
    if (const auto v = node.value<bool>()) {
        out = *v;
    } else if (const auto i = node.value<int64_t>()) {  // 0 / 1 is an easy habit from the old ini
        out = *i != 0;
    } else {
        logx::Write("config: [%s] %s must be true or false, keeping %s", section, key, out ? "true" : "false");
    }
}

static void ReadInt(const toml::table& root, const char* section, const char* key, int lo, int hi, int& out) {
    const auto node = root[section][key];
    if (!node) return;
    if (const auto v = node.value<int64_t>()) {
        out = Clamp(static_cast<int>(*v), lo, hi);
        if (out != *v) logx::Write("config: [%s] %s = %lld is outside %d..%d, using %d", section, key, *v, lo, hi, out);
    } else {
        logx::Write("config: [%s] %s must be a whole number, keeping %d", section, key, out);
    }
}

// Key names of the multiplier tree. Unit and structure groups are spelled the same for both races; the two
// race-specific research groups carry the name a player would look for.
static const char* const kUnitGroupKeys[units::kUnitGroupCount] = {"workers", "melee",      "ranged", "siege", "casters",
                                                                   "air",     "naval",      "demolition", "heroes", nullptr};
static const char* const kStructureKeys[units::kStructureGroupCount] = {"buildings", "building_upgrades"};
static const char* ResearchKey(units::Race race, int group) {
    static const char* const kShared[] = {"melee_upgrades", "ranged_upgrades", "siege_upgrades", "naval_upgrades"};
    if (group < 4) return kShared[group];
    if (group == units::kKnightUpgrades) return race == units::kHuman ? "paladin_upgrades" : "ogre_mage_upgrades";
    return race == units::kHuman ? "mage_spells" : "death_knight_spells";
}
static const char* const kRaceKeys[units::kRaceCount] = {"human", "orc", "neutral"};

static void ReadFactorNode(const toml::node_view<const toml::node> node, const char* where, double& out) {
    if (!node) return;
    const auto v = node.value<double>();  // accepts 2 as well as 2.0
    if (!v || *v < 0.01 || *v > 1000.0) {  // results are clamped to what the engine can store
        logx::Write("config: %s must be a number from 0.01 to 1000, keeping %.2f", where, out);
        return;
    }
    out = *v;
}

static void ReadFactor(const toml::table& root, const char* section, const char* key, double& out) {
    char where[96];
    sprintf_s(where, "[%s] %s", section, key);
    ReadFactorNode(root[section][key], where, out);
}

// [section] all, [section.human] / [section.orc] all + groups, [section.neutral] all.
// healthOnly: the same keys minus research, plus the "heroes" unit group.
static void ReadMultipliers(const toml::table& root, const char* section, bool healthOnly, Multipliers& m) {
    const auto sec = root[section];
    if (!sec) return;
    char where[96];
    std::string topKnown = " all units structures human orc neutral ";
    auto readTop = [&](const char* key, double& out) {
        sprintf_s(where, "[%s] %s", section, key);
        ReadFactorNode(sec[key], where, out);
    };
    readTop("all", m.all);
    readTop("units", m.units);
    readTop("structures", m.structures);
    if (!healthOnly) {  // nothing to research has hit points
        topKnown += "research ";
        readTop("research", m.research);
    }

    const toml::table* secTable = sec.as_table();
    if (secTable)
        for (const auto& [key, unused] : *secTable) {
            (void)unused;
            const std::string padded = " " + std::string(key.str()) + " ";
            if (topKnown.find(padded) == std::string::npos) logx::Write("config: unknown key [%s] %s ignored", section, padded.c_str() + 1);
        }

    for (int r = 0; r < units::kRaceCount; ++r) {
        const auto raceNode = sec[kRaceKeys[r]];
        const toml::table* raceTable = raceNode.as_table();
        if (!raceTable) continue;
        RaceMultipliers& rm = m.race[r];
        std::string known = " all ";
        auto read = [&](const char* key, double& out) {
            known += key;
            known += ' ';
            sprintf_s(where, "[%s.%s] %s", section, kRaceKeys[r], key);
            ReadFactorNode(raceNode[key], where, out);
        };
        sprintf_s(where, "[%s.%s] all", section, kRaceKeys[r]);
        ReadFactorNode(raceNode["all"], where, rm.all);
        if (r != units::kNeutral) {
            read("units", rm.units);
            if (!healthOnly) read("research", rm.research);
            for (int grp = 0; grp < units::kUnitGroupCount; ++grp) {
                if (!kUnitGroupKeys[grp] || (!healthOnly && grp == units::kHeroes)) continue;  // heroes are never trained
                read(kUnitGroupKeys[grp], rm.unit[grp]);
            }
            read("structures", rm.structures);
            for (int grp = 0; grp < units::kStructureGroupCount; ++grp) read(kStructureKeys[grp], rm.structure[grp]);
            if (!healthOnly)
                for (int grp = 0; grp < units::kResearchGroupCount; ++grp)
                    read(ResearchKey(static_cast<units::Race>(r), grp), rm.researchGroup[grp]);
        }
        for (const auto& [key, unused] : *raceTable) {
            (void)unused;
            const std::string padded = " " + std::string(key.str()) + " ";
            if (known.find(padded) == std::string::npos)
                logx::Write("config: unknown key [%s.%s] %s ignored", section, kRaceKeys[r], padded.c_str() + 1);
        }
    }
}

// [range] upgrade_bonus
static void ReadRange(const toml::table& root, Config& c) {
    const auto sec = root["range"];
    if (!sec) return;
    if (const auto node = sec["upgrade_bonus"]) {
        const auto v = node.value<int64_t>();
        if (v && *v >= 0 && *v <= 20) c.rangeUpgradeBonus = static_cast<int>(*v);
        else logx::Write("config: [range] upgrade_bonus must be a whole number from 0 to 20, keeping %d", c.rangeUpgradeBonus);
    }
    if (const toml::table* secTable = sec.as_table())
        for (const auto& [key, unused] : *secTable) {
            (void)unused;
            const std::string k(key.str());
            if (k != "upgrade_bonus") logx::Write("config: unknown key [range] %s ignored", k.c_str());
        }
}

// [spell_cost] / [spell_damage]: all = multiplier, one whole number per spell, -1 = the game's own. 0 is refused (a
// free heal or exorcism divides by zero in the game); above the engine limit the limit is used.
static void ReadSpellNumbers(const toml::table& root, const char* section, const char* const* keys, int count,
                             const int* max, double& all, int* values) {
    if (!root[section]) return;
    ReadFactor(root, section, "all", all);
    for (int i = 0; i < count; ++i) {
        const auto node = root[section][keys[i]];
        if (!node) continue;
        const auto v = node.value<int64_t>();
        if (!v || *v == 0 || *v < -1) {
            logx::Write("config: [%s] %s must be -1 (the game's value) or a whole number from 1 to %d, keeping %d", section, keys[i],
                        max[i], values[i]);
            continue;
        }
        values[i] = *v > max[i] ? max[i] : static_cast<int>(*v);
        if (*v > max[i]) logx::Write("config: [%s] %s = %lld is more than the game can hold, using %d", section, keys[i], *v, max[i]);
    }
}

static void ReadSpells(const toml::table& root, Config& c) {
    int costMax[kSpellCostCount];
    for (int& m : costMax) m = spells::kMaxCost;
    ReadSpellNumbers(root, "spell_cost", kSpellCostKeys, kSpellCostCount, costMax, c.spellCostAll, c.spellCost);
    ReadSpellNumbers(root, "spell_damage", kSpellDamageKeys, kSpellDamageCount, spells::kDamageMax, c.spellDamageAll, c.spellDamage);
    if (const auto node = root["mana"]["regen"]) {
        const auto v = node.value<double>();  // accepts 2 as well as 2.0
        if (v && *v >= 0.1 && *v <= 40.0) c.manaRegen = *v;
        else logx::Write("config: [mana] regen must be a number from 0.1 to 40, keeping %.2f", c.manaRegen);
    }
}

// [unit.<name>] / [building.<name>] stat = value. -1 (or a missing key) keeps the game's own number; 0 is a real
// value where it makes sense. Both sections fill the same per-type table: the ids do not overlap.
static void ReadStatTables(const toml::table& root, const char* section, bool buildings, Config& c) {
    // Structure health stays below 32768: the construction progress maths (FUN_004ed4e0) works in signed 16 bits.
    const int kMax[kStatCount] = {buildings ? 32767 : 65535, 255, 255, 255, 20, 9, 2550, 2550, 2550, 255};
    static const int kMin[kStatCount] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const toml::table* all = root[section].as_table();
    if (!all) return;
    for (const auto& [unitKey, unitNode] : *all) {
        const std::string name(unitKey.str());
        const units::Entry* unit = units::FindByName(name.c_str());
        const units::Building* building = units::FindBuildingByName(name.c_str());
        const toml::table* stats = unitNode.as_table();
        if (!stats || (!unit && !building)) {
            logx::Write("config: [%s.%s] is not a known %s name, section ignored", section, name.c_str(), buildings ? "building" : "unit");
            continue;
        }
        if (buildings != (building != nullptr)) {
            logx::Write("config: %s is a %s: write [%s.%s] instead, section ignored", name.c_str(), building ? "building" : "unit",
                        building ? "building" : "unit", name.c_str());
            continue;
        }
        const uint8_t id = building ? building->id : unit->id;
        for (const auto& [statKey, statNode] : *stats) {
            const std::string key(statKey.str());
            int stat = -1;
            for (int i = 0; i < kStatCount; ++i)
                if (key == kStatKeys[i]) stat = i;
            if (stat < 0) {
                logx::Write("config: unknown key [%s.%s] %s ignored", section, name.c_str(), key.c_str());
                continue;
            }
            const auto v = statNode.value<int64_t>();
            if (!v || *v < -1 || (*v >= 0 && (*v < kMin[stat] || *v > kMax[stat]))) {
                logx::Write("config: [%s.%s] %s must be -1 (game default) or a whole number from %d to %d", section, name.c_str(),
                            key.c_str(), kMin[stat], kMax[stat]);
                continue;
            }
            int value = static_cast<int>(*v);
            if (value > 0 && stat >= kStatGold && stat <= kStatOil && value % 10 != 0) {
                value = (value + 5) / 10 * 10;  // the engine stores prices in tens
                logx::Write("config: [%s.%s] %s rounded to %d (prices move in steps of 10)", section, name.c_str(), key.c_str(), value);
            }
            c.unitStat[id][stat] = value;
        }
    }
}

static void ReadToggleKey(const toml::table& root, const char* section, int& out) {
    const auto node = root[section]["toggle_key"];
    if (!node) return;
    if (const auto v = node.value<int64_t>()) {
        out = Clamp(static_cast<int>(*v), 0, 255);
        return;
    }
    if (const auto s = node.value<std::string>()) {
        int n = 0;
        if ((s->size() == 2 || s->size() == 3) && ((*s)[0] == 'F' || (*s)[0] == 'f') && sscanf_s(s->c_str() + 1, "%d", &n) == 1 &&
            n >= 1 && n <= 12) {
            out = VK_F1 + n - 1;
            return;
        }
        if (s->empty()) {
            out = 0;  // no hotkey
            return;
        }
    }
    logx::Write("config: [%s] toggle_key must be \"F1\"..\"F12\", \"\" or a virtual-key number, keeping %d", section, out);
}

// [auto_production] plus its tables units, bank_multiple, land_tier1..3 and navy_tier1..3. Validates its own keys like
// the multiplier trees: a value out of range or a key in the wrong group is logged and the default is kept.
static void ReadAutoProduction(const toml::table& root, Config& c) {
    const char* const kSec = "auto_production";
    const auto sec = root[kSec];
    if (!sec) return;
    AutoProduction& p = c.production;
    ReadBool(root, kSec, "enabled", p.enabled);
    ReadToggleKey(root, kSec, p.toggleKey);
    for (int tier = 0; tier < kProdTiers; ++tier) {
        char key[16];
        sprintf_s(key, "workers_tier%d", tier + 1);
        ReadInt(root, kSec, key, 0, 200, p.workersTier[tier]);
    }
    ReadInt(root, kSec, "food_free_min", 0, 200, p.foodFreeMin);
    ReadInt(root, kSec, "food_free_percent", 0, 100, p.foodFreePercent);
    ReadInt(root, kSec, "filler_min", 1, 1000, p.fillerMin);
    ReadInt(root, kSec, "save_up_seconds", 0, 600, p.saveUpSeconds);
    ReadInt(root, kSec, "plenty_units", 1, 100, p.plentyUnits);
    ReadInt(root, kSec, "navy_max", 0, 100, p.navyMax);
    ReadBool(root, kSec, "workers_ignore_reserve", p.workersIgnoreReserve);
    ReadBool(root, kSec, "tankers_ignore_reserve", p.tankersIgnoreReserve);
    auto readNumber = [&](const char* key, double lo, double hi, double& out) {  // 0 is a real value here
        const auto node = sec[key];
        if (!node) return;
        const auto v = node.value<double>();
        if (v && *v >= lo && *v <= hi) out = *v;
        else logx::Write("config: [%s] %s must be a number from %.2f to %.2f, keeping %.2f", kSec, key, lo, hi, out);
    };
    readNumber("reserve_extra", 0.0, 10.0, p.reserveExtra);
    readNumber("upgrade_bias", 0.0, 10.0, p.upgradeBias);
    readNumber("navy_weight", 0.0, 10.0, p.navyWeight);
    // [auto_production.bank_multiple] works like [costs]: "all" is the master, a class key overrides it for that class.
    if (const auto node = sec["bank_multiple"]["all"]) {
        const auto v = node.value<double>();
        if (v && *v >= 0.1 && *v <= 1000.0) p.bankMultiple = *v;
        else logx::Write("config: [%s.bank_multiple] all must be a number from 0.1 to 1000", kSec);
    }

    auto warnUnknown = [&](const char* table, const char* known) {
        const toml::table* t = (table ? sec[table] : sec).as_table();
        if (!t) return;
        for (const auto& [key, unused] : *t) {
            (void)unused;
            const std::string padded = " " + std::string(key.str()) + " ";
            if (!strstr(known, padded.c_str()))
                logx::Write("config: unknown key [%s%s%s] %s ignored", kSec, table ? "." : "", table ? table : "", padded.c_str() + 1);
        }
    };
    warnUnknown(nullptr, " enabled toggle_key workers_tier1 workers_tier2 workers_tier3 food_free_min food_free_percent "
                         "bank_multiple reserve_extra "
                         "upgrade_bias filler_min save_up_seconds plenty_units navy_weight navy_max workers_ignore_reserve "
                         "tankers_ignore_reserve units "
                         "no_enemy_navy_cap land_tier1 land_tier2 land_tier3 navy_tier1 navy_tier2 navy_tier3 ");
    const char* const kAllClasses = " workers infantry archers knights casters flyers siege tankers destroyers battleships submarines ";
    const char* const kLandClasses = " infantry archers knights casters flyers siege ";
    const char* const kNavyClasses = " destroyers battleships submarines ";
    char withAll[256];
    sprintf_s(withAll, " all%s", kAllClasses);
    warnUnknown("units", kAllClasses);
    warnUnknown("bank_multiple", withAll);
    warnUnknown("no_enemy_navy_cap", kAllClasses);
    for (int cls = 0; cls < kProdClassCount; ++cls) {
        const char* key = kProductionClassKeys[cls];
        if (const auto node = sec["units"][key]) {
            if (const auto v = node.value<bool>()) p.unitClass[cls] = *v;
            else logx::Write("config: [%s.units] %s must be true or false", kSec, key);
        }
        if (const auto node = sec["bank_multiple"][key]) {
            const auto v = node.value<double>();
            if (v && *v >= 0.1 && *v <= 1000.0) p.classBankMultiple[cls] = *v;
            else logx::Write("config: [%s.bank_multiple] %s must be a number from 0.1 to 1000", kSec, key);
        }
        if (const auto node = sec["no_enemy_navy_cap"][key]) {
            const auto v = node.value<int64_t>();
            if (v && *v >= 0 && *v <= 200) p.noEnemyNavyCap[cls] = static_cast<int>(*v);
            else logx::Write("config: [%s.no_enemy_navy_cap] %s must be a whole number from 0 to 200", kSec, key);
        }
    }
    for (int tier = 0; tier < kProdTiers; ++tier)
        for (int navy = 0; navy < 2; ++navy) {
            char table[16];
            sprintf_s(table, "%s_tier%d", navy ? "navy" : "land", tier + 1);
            warnUnknown(table, navy ? kNavyClasses : kLandClasses);
            for (int cls = 0; cls < kProdClassCount; ++cls) {
                const char* group = navy ? kNavyClasses : kLandClasses;
                const std::string padded = " " + std::string(kProductionClassKeys[cls]) + " ";
                if (!strstr(group, padded.c_str())) continue;  // that class is not part of this group
                const auto node = sec[table][kProductionClassKeys[cls]];
                if (!node) continue;
                const auto v = node.value<int64_t>();
                if (v && *v >= 0 && *v <= 100) (navy ? p.navy : p.land)[tier][cls] = static_cast<int>(*v);
                else logx::Write("config: [%s.%s] %s must be a whole number from 0 to 100 (percent)", kSec, table, kProductionClassKeys[cls]);
            }
        }
}

const char* const kCasterKindKeys[kCasterKindCount] = {"paladin", "mage", "ogre_mage", "death_knight"};
const char* const kUpgradeEffectKeys[kUpgradeEffectCount] = {"missile_damage", "melee_damage", "shields",
                                                             "ship_damage",    "ship_armor",   "siege_damage"};

// [priority]: one list of spell names per caster, plus save_mana. A name that is misspelled or belongs to another
// caster is logged and dropped; the caster's own spells that the list leaves out are appended in the default order,
// so a spell added in a later version is never silently switched off. The first mention of a spell wins.
static void ReadPriority(const toml::table& root, Config& c) {
    ReadBool(root, "priority", "save_mana", c.priority.saveMana);
    const Priority defaults;
    for (int kind = 0; kind < kCasterKindCount; ++kind) {
        const auto node = root["priority"][kCasterKindKeys[kind]];
        if (!node) continue;
        const auto* arr = node.as_array();
        if (!arr) {
            logx::Write("config: [priority] %s must be a list of spell names, keeping the default order", kCasterKindKeys[kind]);
            continue;
        }
        int8_t* out = c.priority.list[kind];
        int n = 0;
        bool taken[kSpellCount] = {};
        for (const auto& item : *arr) {
            const auto name = item.value<std::string>();
            if (!name) {
                logx::Write("config: [priority] %s: every entry must be a spell name in quotes", kCasterKindKeys[kind]);
                continue;
            }
            int spell = -1;
            for (int i = 0; i < kSpellCount; ++i)
                if (*name == kSpellKeys[i]) spell = i;
            if (spell < 0) {
                logx::Write("config: [priority] %s: unknown spell \"%s\" ignored", kCasterKindKeys[kind], name->c_str());
                continue;
            }
            bool mine = false;
            for (int i = 0; i < kSpellCount && defaults.list[kind][i] >= 0; ++i) mine = mine || defaults.list[kind][i] == spell;
            if (!mine) {
                logx::Write("config: [priority] %s: \"%s\" is not a %s spell, ignored", kCasterKindKeys[kind], name->c_str(),
                            kCasterKindKeys[kind]);
                continue;
            }
            if (taken[spell]) continue;  // named twice: the first place in the list is the one that counts
            taken[spell] = true;
            out[n++] = static_cast<int8_t>(spell);
        }
        for (int i = 0; i < kSpellCount && defaults.list[kind][i] >= 0; ++i) {
            const int8_t spell = defaults.list[kind][i];
            if (!taken[spell]) out[n++] = spell;
        }
        if (n < kSpellCount) out[n] = -1;
    }
}

// [weapon_types] / [armor_types]: a name the player invents, and the units and buildings that carry it. Entries
// are the names the [unit.NAME] / [building.NAME] tables accept, plus the four ready-made groups. A unit carries at
// most one weapon type and one armor type; a specific name always beats a group, and among specific names the
// first one wins and the second is logged.
static bool ValidTypeName(const std::string& name) {
    if (name.empty() || name.size() >= kDamageTypeNameLen) return false;
    for (char ch : name)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return false;
    return true;
}

// -1 = not a group name. The groups come from the static unit table, not from the game's flags, so they are the
// same before the first map is loaded.
static int GroupMembers(const std::string& name, bool* out) {
    memset(out, 0, units::kTypeCount);
    int n = 0;
    for (int t = 0; t < units::kTypeCount; ++t) {
        const units::Entry* e = t < units::kFirstBuilding ? units::FindById(static_cast<uint8_t>(t)) : nullptr;
        bool member = false;
        if (name == "structures") member = t >= units::kFirstBuilding;
        else if (name == "ships") member = e && e->group == units::kNaval;
        else if (name == "air_units") member = e && e->group == units::kAir;
        else if (name == "land_units") member = e && e->group != units::kNaval && e->group != units::kAir;
        else return -1;
        out[t] = member;
        n += member;
    }
    return n;
}

static void ReadTypeSection(const toml::table& root, const char* section, char (*names)[kDamageTypeNameLen], int& count,
                            uint8_t* slot, const char* what) {
    const auto sec = root[section];
    if (!sec) return;
    const toml::table* tbl = sec.as_table();
    if (!tbl) {
        logx::Write("config: [%s] must be a table of name = [ ... ] lists", section);
        return;
    }
    // Two passes: the named units first, so a name listed anywhere always beats a ready-made group.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& [key, value] : *tbl) {
            const std::string name(key.str());
            const toml::array* list = value.as_array();
            if (!list) {
                if (pass == 0) logx::Write("config: [%s] %s must be a list of unit names in quotes", section, name.c_str());
                continue;
            }
            if (!ValidTypeName(name)) {
                if (pass == 0)
                    logx::Write("config: [%s] \"%s\" is not a usable type name (a to z, 0 to 9 and _ only, under %d letters)",
                                section, name.c_str(), kDamageTypeNameLen);
                continue;
            }
            int index = -1;
            for (int i = 0; i < count; ++i)
                if (name == names[i]) index = i;
            if (index < 0) {
                if (pass == 1) continue;  // it was refused in pass 0
                if (count >= kMaxDamageTypes) {
                    logx::Write("config: [%s] %s: more than %d %s types, ignored", section, name.c_str(), kMaxDamageTypes, what);
                    continue;
                }
                index = count++;
                strcpy_s(names[index], name.c_str());
            }
            for (const auto& item : *list) {
                const auto member = item.value<std::string>();
                if (!member) {
                    if (pass == 0) logx::Write("config: [%s] %s: every entry must be a name in quotes", section, name.c_str());
                    continue;
                }
                bool group[units::kTypeCount];
                const int groupSize = GroupMembers(*member, group);
                if (groupSize < 0) {  // a single unit or building name
                    if (pass != 0) continue;
                    const units::Entry* e = units::FindByName(member->c_str());
                    const units::Building* b = e ? nullptr : units::FindBuildingByName(member->c_str());
                    const int id = e ? e->id : (b ? b->id : -1);
                    if (id < 0) {
                        logx::Write("config: [%s] %s: unknown unit \"%s\" ignored", section, name.c_str(), member->c_str());
                        continue;
                    }
                    if (slot[id] != kNoDamageType && slot[id] != index) {
                        logx::Write("config: [%s] %s already has the %s type %s, \"%s\" ignored", section,
                                    member->c_str(), what, names[slot[id]], name.c_str());
                        continue;
                    }
                    slot[id] = static_cast<uint8_t>(index);
                } else if (pass == 1) {
                    for (int t = 0; t < units::kTypeCount; ++t)
                        if (group[t] && slot[t] == kNoDamageType) slot[t] = static_cast<uint8_t>(index);
                }
            }
        }
    }
}

static void ReadDamageTypes(const toml::table& root, Config& c) {
    DamageTypes& d = c.damageTypes;
    ReadTypeSection(root, "weapon_types", d.weaponName, d.weaponCount, d.weaponOf, "weapon");
    ReadTypeSection(root, "armor_types", d.armorName, d.armorCount, d.armorOf, "armor");
    const auto sec = root["damage_bonus"];
    if (const toml::table* tbl = sec ? sec.as_table() : nullptr) {
        for (const auto& [key, value] : *tbl) {
            const std::string weapon(key.str());
            int w = -1;
            for (int i = 0; i < d.weaponCount; ++i)
                if (weapon == d.weaponName[i]) w = i;
            const toml::table* row = value.as_table();
            if (!row) {
                logx::Write("config: [damage_bonus.%s] must be a table of armor_type = multiplier", weapon.c_str());
                continue;
            }
            if (w < 0) {
                logx::Write("config: [damage_bonus.%s] there is no weapon type called \"%s\", ignored", weapon.c_str(),
                            weapon.c_str());
                continue;
            }
            for (const auto& [armorKey, armorValue] : *row) {
                const std::string armor(armorKey.str());
                int a = -1;
                for (int i = 0; i < d.armorCount; ++i)
                    if (armor == d.armorName[i]) a = i;
                if (a < 0) {
                    logx::Write("config: [damage_bonus.%s] there is no armor type called \"%s\", ignored", weapon.c_str(),
                                armor.c_str());
                    continue;
                }
                const auto v = armorValue.value<double>();
                if (!v || *v < 0.0 || *v > 10.0) {
                    logx::Write("config: [damage_bonus.%s] %s must be a number from 0 to 10", weapon.c_str(), armor.c_str());
                    continue;
                }
                d.bonus[w][a] = static_cast<uint16_t>(*v * 256.0 + 0.5);
                if (d.bonus[w][a] != kDamageBonusOne) {
                    d.any = true;
                    ++d.bonusCount;
                }
            }
        }
    }
    if (d.weaponCount || d.armorCount) {
        char line[320] = "damage types:";
        for (int i = 0; i < d.weaponCount; ++i) {
            int n = 0;
            for (uint8_t s : d.weaponOf) n += s == i;
            char one[64];
            sprintf_s(one, "%s weapon %s (%d units)", i ? "," : "", d.weaponName[i], n);
            if (strlen(line) + strlen(one) < sizeof(line)) strcat_s(line, one);
        }
        for (int i = 0; i < d.armorCount; ++i) {
            int n = 0;
            for (uint8_t s : d.armorOf) n += s == i;
            char one[64];
            sprintf_s(one, "%s armor %s (%d)", i || d.weaponCount ? "," : "", d.armorName[i], n);
            if (strlen(line) + strlen(one) < sizeof(line)) strcat_s(line, one);
        }
        char tail[32];
        sprintf_s(tail, "; %d bonus%s", d.bonusCount, d.bonusCount == 1 ? "" : "es");
        if (strlen(line) + strlen(tail) < sizeof(line)) strcat_s(line, tail);
        logx::Write("%s", line);
    }
}

static void SetPolymorphTargets(Config& c, const char* const* names, size_t count) {
    memset(c.polymorphRank, 0, sizeof(c.polymorphRank));
    uint8_t rank = 1;
    for (size_t i = 0; i < count && rank < 255; ++i) {
        const units::Entry* e = units::FindByName(names[i]);
        if (!e) {
            logx::Write("config: [polymorph] targets: unknown unit \"%s\" ignored", names[i]);
            continue;
        }
        if (!c.polymorphRank[e->id]) c.polymorphRank[e->id] = rank++;
    }
}

static void SetDefaultHeroes(Config& c) {
    memset(c.isHero, 0, sizeof(c.isHero));
    for (const units::Entry& e : units::kUnits)
        if (e.hero) c.isHero[e.id] = true;
}

// regen_for = "all" | "mine", shared by [heroes] and [unit_regen]
static void ReadRegenFor(const toml::table& root, const char* section, bool& mineOnly) {
    const auto node = root[section]["regen_for"];
    if (!node) return;
    const auto s = node.value<std::string>();
    if (s && _stricmp(s->c_str(), "mine") == 0) mineOnly = true;
    else if (s && _stricmp(s->c_str(), "all") == 0) mineOnly = false;
    else logx::Write("config: [%s] regen_for must be \"all\" or \"mine\", keeping \"%s\"", section, mineOnly ? "mine" : "all");
}

static void ReadHeroes(const toml::table& root, Config& c) {
    if (const auto node = root["heroes"]["units"]) {
        if (const toml::array* arr = node.as_array()) {
            memset(c.isHero, 0, sizeof(c.isHero));
            for (const auto& el : *arr) {
                const auto s = el.value<std::string>();
                const units::Entry* e = s ? units::FindByName(s->c_str()) : nullptr;
                if (e) c.isHero[e->id] = true;
                else logx::Write("config: [heroes] units: unknown unit \"%s\" ignored", s ? s->c_str() : "(not text)");
            }
        } else {
            logx::Write("config: [heroes] units must be a list of unit names, keeping the default list");
        }
    }
    ReadRegenFor(root, "heroes", c.heroRegenMineOnly);
}

static void ReadPolymorphTargets(const toml::table& root, Config& c) {
    const auto node = root["polymorph"]["targets"];
    if (!node) return;
    const toml::array* arr = node.as_array();
    if (!arr) {
        logx::Write("config: [polymorph] targets must be a list of unit names, keeping the default list");
        return;
    }
    std::vector<std::string> owned;
    for (const auto& el : *arr) {
        if (const auto s = el.value<std::string>()) owned.push_back(*s);
        else logx::Write("config: [polymorph] targets: non-text entry ignored");
    }
    std::vector<const char*> names;
    for (const auto& s : owned) names.push_back(s.c_str());
    SetPolymorphTargets(c, names.data(), names.size());
}

// Every key the file may contain, so a typo is reported instead of silently doing nothing.
static void WarnUnknownKeys(const toml::table& root) {
    static const struct {
        const char* section;
        const char* keys;
    } kKnown[] = {
        {"general", " enabled toggle_key interval_ticks log_casts log_ai "},
        {"autocast", " search_radius combat_radius own_units_only cast_while_attacking channel_mana_reserve area_min_enemies "
                     "area_building_value area_friendly_clearance fireball_min_enemies "},
        {"spells", " heal exorcism slow polymorph bloodlust death_coil haste unholy_armor raise_dead holy_vision flame_shield "
                   "fireball invisibility blizzard death_and_decay whirlwind runes "},
        {"heal", " min_missing_hp below_percent "},
        {"polymorph", " targets "},
        {"haste", " flyers_only "},
        {"heroes", " units regen regen_hp_per_second regen_for "},
        {"unit_regen", " enabled hp_per_second regen_for "},
        {"eye_of_kilrogg", " cast cast_at_mana max_active auto_scout "},
        {"gold_mines", " unlimited amount "},
        {"food", " hall_food hall_food_amount "},
        {"oil_platforms", " unlimited amount "},
        {"health", nullptr},  // multiplier trees and [range] validate their own keys
        {"costs", nullptr},
        {"time", nullptr},
        {"range", nullptr},
        {"unit", nullptr},
        {"building", nullptr},
        {"workers", " auto_harvest harvest_idle_seconds harvest_radius auto_repair repair_idle_seconds repair_radius "},
        {"trees", " regrow regrow_min_minutes regrow_max_minutes building_distance unit_distance "},
        {"spell_cost", " all holy_vision heal exorcism flame_shield fireball slow invisibility polymorph blizzard eye_of_kilrogg "
                       "bloodlust raise_dead death_coil whirlwind haste unholy_armor runes death_and_decay "},
        {"spell_damage", " all fireball flame_shield blizzard death_and_decay whirlwind death_coil runes heal "},
        {"mana", " regen "},
        {"auto_production", nullptr},  // validates its own keys and sub-tables
        {"priority", " save_mana paladin mage ogre_mage death_knight "},
        {"upgrades", " missile_damage melee_damage shields ship_damage ship_armor siege_damage "},
        {"weapon_types", nullptr},   // the keys are names the player invents; the reader validates them
        {"armor_types", nullptr},
        {"damage_bonus", nullptr},
    };
    for (const auto& [sectionKey, sectionNode] : root) {
        const std::string section(sectionKey.str());
        const char* keys = nullptr;
        for (const auto& k : kKnown)
            if (section == k.section) keys = k.keys;
        bool known = false;
        for (const auto& k : kKnown)
            if (section == k.section) known = true;
        if (!known) {
            logx::Write("config: unknown section [%s] ignored", section.c_str());
            continue;
        }
        if (!keys) continue;
        const toml::table* tbl = sectionNode.as_table();
        if (!tbl) continue;
        for (const auto& [key, unused] : *tbl) {
            (void)unused;
            const std::string padded = " " + std::string(key.str()) + " ";
            if (!strstr(keys, padded.c_str())) logx::Write("config: unknown key [%s] %s ignored", section.c_str(), padded.c_str() + 1);
        }
    }
}

static bool Load() {
    toml::parse_result result = toml::parse_file(std::wstring_view(g_path));
    if (!result) {
        const auto& err = result.error();
        logx::Write("config: gameplay_options.toml line %u column %u: %s. Previous settings stay in force.",
                    static_cast<unsigned>(err.source().begin.line), static_cast<unsigned>(err.source().begin.column),
                    std::string(err.description()).c_str());
        return false;
    }
    const toml::table& root = result.table();

    Config c;
    SetPolymorphTargets(c, kDefaultPolymorphTargets, sizeof(kDefaultPolymorphTargets) / sizeof(kDefaultPolymorphTargets[0]));
    SetDefaultHeroes(c);
    WarnUnknownKeys(root);
    ReadBool(root, "general", "enabled", c.enabled);
    ReadToggleKey(root, "general", c.toggleKey);
    ReadInt(root, "general", "interval_ticks", 1, 500, c.intervalTicks);
    ReadBool(root, "general", "log_casts", c.logCasts);
    ReadBool(root, "general", "log_ai", c.logAi);
    ReadInt(root, "autocast", "search_radius", 1, 15, c.searchRadius);
    ReadInt(root, "autocast", "combat_radius", 1, 15, c.combatRadius);
    ReadBool(root, "autocast", "own_units_only", c.ownUnitsOnly);
    ReadBool(root, "autocast", "cast_while_attacking", c.castWhileAttacking);
    ReadInt(root, "autocast", "channel_mana_reserve", 0, 255, c.channelManaReserve);
    ReadInt(root, "autocast", "area_min_enemies", 1, 50, c.areaMinEnemies);
    ReadInt(root, "autocast", "area_building_value", 1, 20, c.areaBuildingValue);
    ReadInt(root, "autocast", "area_friendly_clearance", 0, 6, c.areaFriendlyClearance);
    ReadInt(root, "autocast", "fireball_min_enemies", 1, 50, c.fireballMinEnemies);
    for (int i = 0; i < kSpellCount; ++i) ReadBool(root, "spells", kSpellKeys[i], c.spell[i]);
    ReadInt(root, "heal", "min_missing_hp", 1, 65535, c.healMinMissingHp);
    ReadInt(root, "heal", "below_percent", 1, 100, c.healBelowPct);
    ReadPolymorphTargets(root, c);
    ReadBool(root, "haste", "flyers_only", c.hasteFlyersOnly);
    ReadBool(root, "eye_of_kilrogg", "cast", c.eyeCast);
    ReadInt(root, "eye_of_kilrogg", "cast_at_mana", 1, 255, c.eyeCastAtMana);
    ReadInt(root, "eye_of_kilrogg", "max_active", 1, 50, c.eyeMaxActive);
    ReadBool(root, "eye_of_kilrogg", "auto_scout", c.eyeAutoScout);
    ReadBool(root, "gold_mines", "unlimited", c.goldMinesUnlimited);
    ReadBool(root, "oil_platforms", "unlimited", c.oilPlatformsUnlimited);
    ReadFactor(root, "gold_mines", "amount", c.goldMinesAmount);
    ReadBool(root, "food", "hall_food", c.hallFood);
    ReadInt(root, "food", "hall_food_amount", 1, 200, c.hallFoodAmount);
    ReadFactor(root, "oil_platforms", "amount", c.oilAmount);
    ReadMultipliers(root, "health", true, c.health);
    ReadMultipliers(root, "costs", false, c.costs);
    ReadMultipliers(root, "time", false, c.time);
    ReadRange(root, c);
    ReadSpells(root, c);
    ReadStatTables(root, "unit", false, c);
    ReadStatTables(root, "building", true, c);
    ReadBool(root, "workers", "auto_harvest", c.workerAutoHarvest);
    ReadInt(root, "workers", "harvest_idle_seconds", 0, 3600, c.workerHarvestIdleSeconds);
    ReadInt(root, "workers", "harvest_radius", 1, 64, c.workerHarvestRadius);
    ReadBool(root, "workers", "auto_repair", c.workerAutoRepair);
    ReadInt(root, "workers", "repair_idle_seconds", 0, 3600, c.workerRepairIdleSeconds);
    ReadInt(root, "workers", "repair_radius", 1, 64, c.workerRepairRadius);
    ReadBool(root, "trees", "regrow", c.treesRegrow);
    ReadInt(root, "trees", "regrow_min_minutes", 1, 600, c.treesRegrowMinMinutes);
    ReadInt(root, "trees", "regrow_max_minutes", 1, 600, c.treesRegrowMaxMinutes);
    if (c.treesRegrowMaxMinutes < c.treesRegrowMinMinutes) {
        // Only worth a note when the file asked for it; a lone regrow_min_minutes above the default maximum is fine.
        if (root["trees"]["regrow_max_minutes"])
            logx::Write("config: [trees] regrow_max_minutes = %d is below regrow_min_minutes = %d, using %d for both",
                        c.treesRegrowMaxMinutes, c.treesRegrowMinMinutes, c.treesRegrowMinMinutes);
        c.treesRegrowMaxMinutes = c.treesRegrowMinMinutes;
    }
    ReadInt(root, "trees", "building_distance", 0, 10, c.treesBuildingDistance);
    ReadInt(root, "trees", "unit_distance", 0, 10, c.treesUnitDistance);
    ReadInt(root, "heroes", "regen_hp_per_second", 0, 1000, c.heroRegenPerSecond);
    // Files written before the "regen" switch existed turned hero regeneration on with a number above 0.
    if (root["heroes"]["regen"]) ReadBool(root, "heroes", "regen", c.heroRegen);
    else if (root["heroes"]["regen_hp_per_second"]) c.heroRegen = c.heroRegenPerSecond > 0;
    ReadBool(root, "unit_regen", "enabled", c.unitRegen);
    ReadInt(root, "unit_regen", "hp_per_second", 0, 1000, c.unitRegenPerSecond);
    ReadRegenFor(root, "unit_regen", c.unitRegenMineOnly);
    ReadHeroes(root, c);
    // [upgrades]: -1 keeps the game's number, otherwise 0..100 (the damage path clamps at 255 and a level
    // counter reaches 2, so no product can wrap).
    for (int i = 0; i < kUpgradeEffectCount; ++i)
        ReadInt(root, "upgrades", kUpgradeEffectKeys[i], -1, 100, c.upgradeEffect[i]);
    ReadDamageTypes(root, c);
    ReadPriority(root, c);
    ReadAutoProduction(root, c);
    g = c;

    char spells[320] = "";
    for (int i = 0; i < kSpellCount; ++i) {
        if (!g.spell[i]) continue;
        strcat_s(spells, kSpellKeys[i]);
        strcat_s(spells, " ");
    }
    int polyCount = 0;
    for (uint8_t r : g.polymorphRank) polyCount += r != 0;
    logx::Write("config: enabled=%d interval=%d radius=%d combat=%d own_only=%d while_attacking=%d heal_missing>=%d "
                "heal<=%d%% poly_targets=%d haste_flyers=%d channel_reserve=%d area_min=%d fireball_min=%d spells: %s",
                g.enabled, g.intervalTicks, g.searchRadius, g.combatRadius, g.ownUnitsOnly, g.castWhileAttacking,
                g.healMinMissingHp, g.healBelowPct, polyCount, g.hasteFlyersOnly, g.channelManaReserve, g.areaMinEnemies,
                g.fireballMinEnemies, spells);
    return true;
}

static bool ReadMtime(FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(g_path, GetFileExInfoStandard, &fad)) return false;
    *out = fad.ftLastWriteTime;
    return true;
}

bool Init(const wchar_t* dllDir) {
    swprintf_s(g_path, L"%s\\gameplay_options.toml", dllDir);
    if (GetFileAttributesW(g_path) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(g_path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            // config/gameplay_options.default.toml, embedded as a resource so the zip and the DLL can never disagree.
            HMODULE self = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&Init), &self);
            if (HRSRC res = FindResourceW(self, L"DEFAULT_TOML", MAKEINTRESOURCEW(10) /* RT_RCDATA */)) {
                DWORD written;
                WriteFile(f, LockResource(LoadResource(self, res)), SizeofResource(self, res), &written, nullptr);
            }
            CloseHandle(f);
        }
    }
    ReadMtime(&g_mtime);
    // Defaults must be complete even if the very first parse fails; later failures keep whatever was loaded before.
    static bool defaultsSet = false;
    if (!defaultsSet) {
        defaultsSet = true;
        SetPolymorphTargets(g, kDefaultPolymorphTargets, sizeof(kDefaultPolymorphTargets) / sizeof(kDefaultPolymorphTargets[0]));
        SetDefaultHeroes(g);
    }
    return Load();
}

int ReloadIfChanged() {
    FILETIME now;
    if (!ReadMtime(&now) || CompareFileTime(&now, &g_mtime) == 0) return 0;
    g_mtime = now;
    return Load() ? 1 : -1;
}

}  // namespace config
