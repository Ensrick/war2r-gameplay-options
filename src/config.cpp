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
    };
    for (const auto& [sectionKey, sectionNode] : root) {
        const std::string section(sectionKey.str());
        const char* keys = nullptr;
        for (const auto& k : kKnown)
            if (section == k.section) keys = k.keys;
        if (!keys) {
            logx::Write("config: unknown section [%s] ignored", section.c_str());
            continue;
        }
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
        logx::Write("config: autocast.toml line %u column %u: %s. Previous settings stay in force.",
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
    swprintf_s(g_path, L"%s\\autocast.toml", dllDir);
    if (GetFileAttributesW(g_path) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(g_path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            // config/autocast.default.toml, embedded as a resource so the zip and the DLL can never disagree.
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
