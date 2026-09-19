#pragma once
#include <cstdint>

#include "units.h"

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

// Keys of a [unit.<name>] table, same order as config::kStatKeys.
enum UnitStat {
    kStatHitPoints, kStatArmor, kStatBasicDamage, kStatPiercingDamage, kStatRange, kStatSight,
    kStatGold, kStatLumber, kStatOil, kStatBuildTime, kStatCount
};

// One multiplier tree, used three times: [health], [costs], [time].
// Effective multiplier = all x race.all x (race.units | race.research umbrella) x group. Everything defaults to 1.0.
struct RaceMultipliers {
    double all = 1.0;         // everything of the race: units (ships included), structures, research
    double units = 1.0;       // umbrella over every unit group
    double structures = 1.0;  // umbrella over both structure groups
    double research = 1.0;    // umbrella over every research group ([costs] / [time])
    double unit[units::kUnitGroupCount] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    double structure[units::kStructureGroupCount] = {1, 1};
    double researchGroup[units::kResearchGroupCount] = {1, 1, 1, 1, 1, 1};
};

struct Multipliers {
    double all = 1.0;         // everything: units (ships included), structures, research. Same in all three sections
    double units = 1.0;       // masters per kind, all races
    double structures = 1.0;
    double research = 1.0;
    RaceMultipliers race[units::kRaceCount];

    double Unit(units::Race r, units::UnitGroup g) const { return all * units * race[r].all * race[r].units * race[r].unit[g]; }
    double Structure(units::Race r, units::StructureGroup g) const {
        return all * structures * race[r].all * race[r].structures * race[r].structure[g];
    }
    double Research(units::Race r, units::ResearchGroup g) const {
        return all * research * race[r].all * race[r].research * race[r].researchGroup[g];
    }
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
    // Out of the box only Heal, Slow, Bloodlust and Raise Dead run; everything else is opt-in.
    bool spell[kSpellCount] = {true, false, true, false, true, false, false, false, true};

    // [heal]
    int healMinMissingHp = 10;    // a scratch is not worth a paladin's attention
    int healBelowPct = 100;       // optional extra gate, 100 = off

    // [polymorph] targets: rank by unit type, 1 = first choice, 0 = never
    uint8_t polymorphRank[256] = {};

    // [haste]
    bool hasteFlyersOnly = true;

    // [eye_of_kilrogg]
    bool eyeCast = false;
    int eyeCastAtMana = 255;        // the computer only casts it at full mana
    int eyeMaxActive = 1;
    bool eyeAutoScout = false;

    // [gold_mines] / [oil_platforms]
    bool goldMinesUnlimited = false;
    bool oilPlatformsUnlimited = false;
    // [food]
    bool hallFood = false;            // halls give hallFoodAmount food instead of the game's 1
    int hallFoodAmount = 5;           // per town hall / keep / castle (and the orc ones); a farm gives 4
    double goldMinesAmount = 1.0;     // x the gold in every mine, once, when a new map starts
    double oilAmount = 1.0;           // x the oil in every patch and platform, likewise

    // [workers]
    bool workerAutoHarvest = false;
    int workerHarvestIdleSeconds = 10;
    int workerHarvestRadius = 5;
    bool workerAutoRepair = true;
    int workerRepairIdleSeconds = 1;
    int workerRepairRadius = 10;

    // [trees]
    bool treesRegrow = false;         // felled forest grows back
    int treesRegrowMinMinutes = 10;   // play time a stump waits, counted from when the mod first saw it: every stump
    int treesRegrowMaxMinutes = 20;   // draws its own wait from min..max, so a felled patch fills back in gradually
    int treesBuildingDistance = 3;    // no regrowth this close (in tiles) to a building or a wall
    int treesUnitDistance = 3;        // nor this close to a ground unit (flyers do not count)

    // [health] [costs] [time] [unit.*]: applied once when a NEW map starts (a savegame keeps the values it was made with)
    // 1.0 = the game's own numbers. In [health], "all" and the unit groups never touch structures; structures only
    // follow their own two keys (buildings, building_upgrades).
    Multipliers health, costs, time;

    // [range]: what Longbow / Lighter Axes add to attack range
    int rangeUpgradeBonus = 1;

    // [unit.<name>] and [building.<name>]: base stats by type id, -1 = the game's own value. The multipliers apply on top.
    int32_t unitStat[256][kStatCount];
    Config() {
        for (auto& row : unitStat)
            for (int32_t& v : row) v = -1;
    }

    // [heroes]
    bool isHero[256] = {};          // unit types listed in [heroes] units
    int heroRegenPerSecond = 0;     // 0 = off
    bool heroRegenMineOnly = false; // regen_for = "mine"
};

namespace config {

extern Config g;
extern const char* const kSpellKeys[kSpellCount];
extern const char* const kStatKeys[kStatCount];

// Loads <dir>\gameplay_options.toml, writing the default file first if it is missing.
// Returns false when the file has a syntax error (the previous / default settings stay in force).
bool Init(const wchar_t* dllDir);
// Re-reads the file when its timestamp changed. 0 = unchanged, 1 = reloaded, -1 = changed but has a syntax error.
int ReloadIfChanged();

}  // namespace config
