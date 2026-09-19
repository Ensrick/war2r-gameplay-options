#include "config.h"

#include <windows.h>
#include <cstdio>
#include <string>

#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#include "../third_party/tomlplusplus/toml.hpp"

#include "log.h"
#include "units.h"

namespace config {

Config g;
const char* const kSpellKeys[kSpellCount] = {"heal",       "exorcism", "slow",         "polymorph", "bloodlust",
                                             "death_coil", "haste",    "unholy_armor", "raise_dead"};

const char* const kStatKeys[kStatCount] = {"hit_points", "armor", "basic_damage", "piercing_damage", "range",
                                           "sight",      "gold",  "lumber",       "oil",             "build_time"};

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

static void ReadToggleKey(const toml::table& root, int& out) {
    const auto node = root["general"]["toggle_key"];
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
    logx::Write("config: [general] toggle_key must be \"F1\"..\"F12\", \"\" or a virtual-key number, keeping %d", out);
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
    if (const auto node = root["heroes"]["regen_for"]) {
        const auto s = node.value<std::string>();
        if (s && _stricmp(s->c_str(), "mine") == 0) c.heroRegenMineOnly = true;
        else if (s && _stricmp(s->c_str(), "all") == 0) c.heroRegenMineOnly = false;
        else logx::Write("config: [heroes] regen_for must be \"all\" or \"mine\", keeping \"%s\"", c.heroRegenMineOnly ? "mine" : "all");
    }
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
        {"general", " enabled toggle_key interval_ticks log_casts "},
        {"autocast", " search_radius combat_radius own_units_only cast_while_attacking "},
        {"spells", " heal exorcism slow polymorph bloodlust death_coil haste unholy_armor raise_dead "},
        {"heal", " min_missing_hp below_percent "},
        {"polymorph", " targets "},
        {"haste", " flyers_only "},
        {"heroes", " units regen_hp_per_second regen_for "},
        {"eye_of_kilrogg", " cast cast_at_mana max_active auto_scout "},
        {"gold_mines", " unlimited amount "},
        {"oil_platforms", " unlimited amount "},
        {"health", nullptr},  // multiplier trees and [range] validate their own keys
        {"costs", nullptr},
        {"time", nullptr},
        {"range", nullptr},
        {"unit", nullptr},
        {"building", nullptr},
        {"workers", " auto_harvest harvest_idle_seconds harvest_radius auto_repair repair_idle_seconds repair_radius "},
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
    ReadToggleKey(root, c.toggleKey);
    ReadInt(root, "general", "interval_ticks", 1, 500, c.intervalTicks);
    ReadBool(root, "general", "log_casts", c.logCasts);
    ReadInt(root, "autocast", "search_radius", 1, 15, c.searchRadius);
    ReadInt(root, "autocast", "combat_radius", 1, 15, c.combatRadius);
    ReadBool(root, "autocast", "own_units_only", c.ownUnitsOnly);
    ReadBool(root, "autocast", "cast_while_attacking", c.castWhileAttacking);
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
    ReadFactor(root, "oil_platforms", "amount", c.oilAmount);
    ReadMultipliers(root, "health", true, c.health);
    ReadMultipliers(root, "costs", false, c.costs);
    ReadMultipliers(root, "time", false, c.time);
    ReadRange(root, c);
    ReadStatTables(root, "unit", false, c);
    ReadStatTables(root, "building", true, c);
    ReadBool(root, "workers", "auto_harvest", c.workerAutoHarvest);
    ReadInt(root, "workers", "harvest_idle_seconds", 0, 3600, c.workerHarvestIdleSeconds);
    ReadInt(root, "workers", "harvest_radius", 1, 64, c.workerHarvestRadius);
    ReadBool(root, "workers", "auto_repair", c.workerAutoRepair);
    ReadInt(root, "workers", "repair_idle_seconds", 0, 3600, c.workerRepairIdleSeconds);
    ReadInt(root, "workers", "repair_radius", 1, 64, c.workerRepairRadius);
    ReadInt(root, "heroes", "regen_hp_per_second", 0, 1000, c.heroRegenPerSecond);
    ReadHeroes(root, c);
    g = c;

    char spells[200] = "";
    for (int i = 0; i < kSpellCount; ++i) {
        if (!g.spell[i]) continue;
        strcat_s(spells, kSpellKeys[i]);
        strcat_s(spells, " ");
    }
    int polyCount = 0;
    for (uint8_t r : g.polymorphRank) polyCount += r != 0;
    logx::Write("config: enabled=%d interval=%d radius=%d combat=%d own_only=%d while_attacking=%d heal_missing>=%d "
                "heal<=%d%% poly_targets=%d haste_flyers=%d spells: %s",
                g.enabled, g.intervalTicks, g.searchRadius, g.combatRadius, g.ownUnitsOnly, g.castWhileAttacking,
                g.healMinMissingHp, g.healBelowPct, polyCount, g.hasteFlyersOnly, spells);
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
