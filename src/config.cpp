#include "config.h"

#include <windows.h>
#include <cstdio>

#include "log.h"

namespace config {

Config g;
const char* const kSpellKeys[kSpellCount] = {"heal",       "exorcism", "slow",         "polymorph", "bloodlust",
                                             "death_coil", "haste",    "unholy_armor", "raise_dead"};

static wchar_t g_path[MAX_PATH];
static FILETIME g_mtime;

static const char kDefaultIni[] =
    "; Warcraft II Remastered Autocast. Edits are picked up while the game runs (within a few seconds).\r\n"
    "; Single-player only: the mod switches itself off in multiplayer games.\r\n"
    "\r\n"
    "[general]\r\n"
    "enabled = 1\r\n"
    "; Ctrl + this key toggles autocast in game. Virtual-key code, 120 = F9.\r\n"
    "toggle_key = 120\r\n"
    "; Game steps between autocast passes. Lower = snappier, the computer AI itself uses 50.\r\n"
    "interval_ticks = 10\r\n"
    "; How far (tiles) a caster looks for targets. It walks into range if needed.\r\n"
    "search_radius = 8\r\n"
    "; Bloodlust / Haste / Unholy Armor only go on units that are fighting: attacking with an enemy this close.\r\n"
    "combat_radius = 6\r\n"
    "; 1 = Heal / Bloodlust / Haste only on your own units, 0 = allies too.\r\n"
    "own_units_only = 1\r\n"
    "; 1 = casters may interrupt their own attack to cast. Move orders are never interrupted.\r\n"
    "cast_while_attacking = 1\r\n"
    "log_casts = 0\r\n"
    "\r\n"
    "[spells]\r\n"
    "heal = 1\r\n"
    "exorcism = 1\r\n"
    "slow = 1\r\n"
    "polymorph = 1\r\n"
    "bloodlust = 1\r\n"
    "death_coil = 1\r\n"
    "haste = 1\r\n"
    "unholy_armor = 0\r\n"
    "; Raise Dead is only cast while an enemy is within search_radius, so skeletons are not wasted.\r\n"
    "raise_dead = 1\r\n"
    "\r\n"
    "[tuning]\r\n"
    "; Heal only units missing at least this many hit points.\r\n"
    "heal_min_missing_hp = 10\r\n"
    "; Extra gate: also require the unit to be at or below this percent of max HP. 100 = off.\r\n"
    "heal_below_pct = 100\r\n"
    "; Polymorph only ground unit types with at least this much max HP. Enemy flyers (dragons, gryphons, daemons)\r\n"
    "; and casters always qualify and are picked first.\r\n"
    "polymorph_min_hp = 90\r\n"
    "; 1 = Haste only on your flying units, when they are sent to attack or are fighting. 0 = any fighting unit.\r\n"
    "haste_flyers_only = 1\r\n";

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void Load() {
    Config c;
    auto readInt = [](const wchar_t* sec, const wchar_t* key, int def) {
        return static_cast<int>(GetPrivateProfileIntW(sec, key, def, g_path));
    };
    c.enabled = readInt(L"general", L"enabled", 1) != 0;
    c.toggleKey = Clamp(readInt(L"general", L"toggle_key", c.toggleKey), 0, 255);
    c.intervalTicks = Clamp(readInt(L"general", L"interval_ticks", c.intervalTicks), 1, 500);
    c.searchRadius = Clamp(readInt(L"general", L"search_radius", c.searchRadius), 1, 15);
    c.combatRadius = Clamp(readInt(L"general", L"combat_radius", c.combatRadius), 1, 15);
    c.ownUnitsOnly = readInt(L"general", L"own_units_only", 1) != 0;
    c.castWhileAttacking = readInt(L"general", L"cast_while_attacking", 1) != 0;
    c.logCasts = readInt(L"general", L"log_casts", 0) != 0;
    for (int i = 0; i < kSpellCount; ++i) {
        wchar_t key[32];
        swprintf_s(key, L"%hs", kSpellKeys[i]);
        c.spell[i] = readInt(L"spells", key, c.spell[i] ? 1 : 0) != 0;
    }
    c.healMinMissingHp = Clamp(readInt(L"tuning", L"heal_min_missing_hp", c.healMinMissingHp), 1, 65535);
    c.healBelowPct = Clamp(readInt(L"tuning", L"heal_below_pct", c.healBelowPct), 1, 100);
    c.polymorphMinHp = Clamp(readInt(L"tuning", L"polymorph_min_hp", c.polymorphMinHp), 0, 65535);
    c.hasteFlyersOnly = readInt(L"tuning", L"haste_flyers_only", 1) != 0;
    g = c;

    char spells[160] = "";
    for (int i = 0; i < kSpellCount; ++i) {
        if (!g.spell[i]) continue;
        strcat_s(spells, kSpellKeys[i]);
        strcat_s(spells, " ");
    }
    logx::Write("config: enabled=%d interval=%d radius=%d combat=%d own_only=%d while_attacking=%d heal_missing>=%d "
                "heal<=%d%% poly_hp>=%d haste_flyers=%d spells: %s",
                g.enabled, g.intervalTicks, g.searchRadius, g.combatRadius, g.ownUnitsOnly, g.castWhileAttacking,
                g.healMinMissingHp, g.healBelowPct, g.polymorphMinHp, g.hasteFlyersOnly, spells);
}

static bool ReadMtime(FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(g_path, GetFileExInfoStandard, &fad)) return false;
    *out = fad.ftLastWriteTime;
    return true;
}

void Init(const wchar_t* dllDir) {
    swprintf_s(g_path, L"%s\\autocast.ini", dllDir);
    if (GetFileAttributesW(g_path) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(g_path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(f, kDefaultIni, sizeof(kDefaultIni) - 1, &written, nullptr);
            CloseHandle(f);
        }
    }
    ReadMtime(&g_mtime);
    Load();
}

bool ReloadIfChanged() {
    FILETIME now;
    if (!ReadMtime(&now) || CompareFileTime(&now, &g_mtime) == 0) return false;
    g_mtime = now;
    Load();
    return true;
}

}  // namespace config
