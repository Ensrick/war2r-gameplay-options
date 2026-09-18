#pragma once
#include <cstdint>

enum Spell {
    kSpellHeal,
    kSpellExorcism,
    kSpellSlow,
    kSpellPolymorph,
    kSpellBloodlust,
    kSpellDeathCoil,
    kSpellHaste,
    kSpellUnholyArmor,
    kSpellRaiseDead,
    kSpellCount
};

struct Config {
    // [general]
    bool enabled = true;
    int toggleKey = 0x78;         // VK_F9, pressed together with Ctrl
    int intervalTicks = 10;       // game steps between autocast passes
    bool logCasts = false;

    // [autocast]
    int searchRadius = 8;         // tiles around the caster
    int combatRadius = 6;         // an ally counts as fighting when an enemy is this close to it
    bool ownUnitsOnly = true;     // friendly spells skip allied players' units
    bool castWhileAttacking = true;

    // [spells]
    bool spell[kSpellCount] = {true, true, true, true, true, true, true, false, true};

    // [heal]
    int healMinMissingHp = 10;    // a scratch is not worth a paladin's attention
    int healBelowPct = 100;       // optional extra gate, 100 = off

    // [polymorph] targets: rank by unit type, 1 = first choice, 0 = never
    uint8_t polymorphRank[256] = {};

    // [haste]
    bool hasteFlyersOnly = true;

    // [eye_of_kilrogg]
    bool eyeCast = true;
    int eyeCastAtMana = 255;        // the computer only casts it at full mana
    int eyeMaxActive = 1;
    bool eyeAutoScout = true;

    // [gold_mines]
    bool goldMinesUnlimited = false;

    // [workers]
    bool workerAutoHarvest = true;
    int workerHarvestIdleSeconds = 10;
    int workerHarvestRadius = 5;
    bool workerAutoRepair = true;
    int workerRepairIdleSeconds = 1;
    int workerRepairRadius = 10;

    // [health] [costs] [vision]: applied once when a NEW map starts (a savegame keeps the values it was made with)
    double hpUnits = 2.0, hpHeroes = 4.0, hpBuildings = 1.0;
    double costUnits = 0.5, costRangedUpgrades = 0.5, costSiegeUpgrades = 0.5;
    uint8_t sightBonus[256] = {};   // extra sight range by unit type

    // [heroes]
    bool isHero[256] = {};          // unit types listed in [heroes] units
    int heroRegenPerSecond = 1;     // 0 = off
    bool heroRegenMineOnly = false; // regen_for = "mine"
};

namespace config {

extern Config g;
extern const char* const kSpellKeys[kSpellCount];

// Loads <dir>\autocast.toml, writing the default file first if it is missing.
// Returns false when the file has a syntax error (the previous / default settings stay in force).
bool Init(const wchar_t* dllDir);
// Re-reads the file when its timestamp changed. 0 = unchanged, 1 = reloaded, -1 = changed but has a syntax error.
int ReloadIfChanged();

}  // namespace config
