#include "datatweaks.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "aiwatch.h"
#include "config.h"
#include "log.h"
#include "mod.h"
#include "production.h"
#include "resume.h"
#include "spells.h"
#include "trees.h"
#include "tweaks.h"
#include "autocast.h"
#include "units.h"
#include "upgrades.h"
#include "world.h"

using namespace game;

namespace datatweaks {

namespace {

// What the engine can store. HP is an unsigned 16-bit word in the type table and in the unit, and the damage code
// (FUN_004bd8f0) compares and subtracts it unsigned; every direct read of unit+0x22 in the exe is a movzx.
// Cosmetic only: the status panel stops printing HP numbers from 10000 up.
constexpr int kMaxHp = 65535;
constexpr int kMaxStructureHp = 32767;   // the construction progress maths (FUN_004ed4e0) is signed 16-bit
constexpr int kMaxTypeCost = 255;        // unit / structure prices are a BYTE holding price / 10: 2550 at most
constexpr int kMaxResearchCost = 65535;  // research prices are plain 16-bit words
constexpr int kMaxTime = 255;            // build and research times are bytes (the game doubles them into its timer)

// Scales a stored value. Zero stays zero (free stays free, "no build time" stays none), anything else stays >= 1.
template <typename T>
void ScaleCell(T& cell, double factor, int cap) {
    if (!cell || factor == 1.0) return;
    const int scaled = static_cast<int>(cell * factor + 0.5);
    cell = static_cast<T>(scaled < 1 ? 1 : (scaled > cap ? cap : scaled));
}

// Race and group of a mobile unit type; false for unused slots.
bool ClassifyUnit(int type, units::Race* race, units::UnitGroup* group) {
    const units::Entry* e = units::FindById(static_cast<uint8_t>(type));
    if (!e) return false;
    *race = e->race;
    // The [heroes] units list decides who is a hero; a campaign hero taken off that list is just "other".
    *group = config::g.isHero[type] ? units::kHeroes : (e->group == units::kHeroes ? units::kOther : e->group);
    return true;
}

void ScaleUnits() {
    const Config& c = config::g;
    uint16_t* hp = At<uint16_t>(kRvaMaxHpByType);
    uint8_t* gold = At<uint8_t>(kRvaGoldCostByType);
    uint8_t* lumber = At<uint8_t>(kRvaLumberCostByType);
    uint8_t* oil = At<uint8_t>(kRvaOilCostByType);
    uint8_t* buildTime = At<uint8_t>(kRvaBuildTimeByType);
    for (int t = 0; t < units::kFirstBuilding; ++t) {
        units::Race race;
        units::UnitGroup group;
        if (!ClassifyUnit(t, &race, &group)) continue;
        ScaleCell(hp[t], c.health.Unit(race, group), kMaxHp);
        ScaleCell(gold[t], c.costs.Unit(race, group), kMaxTypeCost);
        ScaleCell(lumber[t], c.costs.Unit(race, group), kMaxTypeCost);
        ScaleCell(oil[t], c.costs.Unit(race, group), kMaxTypeCost);
        ScaleCell(buildTime[t], c.time.Unit(race, group), kMaxTime);
    }
}

// A structure upgrade is priced and timed as the type it turns into: the pay path FUN_004ac610 reads the same
// per-type tables for training, placement and structure upgrades.
// Since 1.0.8 [health] works like [costs] and [time]: "all" reaches units AND structures, "units" / "structures" are
// the masters per kind.
void ScaleStructures() {
    const Config& c = config::g;
    uint16_t* hp = At<uint16_t>(kRvaMaxHpByType);
    uint8_t* gold = At<uint8_t>(kRvaGoldCostByType);
    uint8_t* lumber = At<uint8_t>(kRvaLumberCostByType);
    uint8_t* oil = At<uint8_t>(kRvaOilCostByType);
    uint8_t* buildTime = At<uint8_t>(kRvaBuildTimeByType);
    for (int t = units::kFirstBuilding; t < units::kTypeCount; ++t) {
        const units::Race race = units::StructureRace(t);
        const units::StructureGroup group = units::StructureGroupOf(t);
        // Neutral structures (gold mine 25500, dark portal, runestone) are scenery, not buildings: never scaled.
        if (race != units::kNeutral) ScaleCell(hp[t], c.health.Structure(race, group), kMaxStructureHp);
        const double cost = c.costs.Structure(race, group);
        ScaleCell(gold[t], cost, kMaxTypeCost);
        ScaleCell(lumber[t], cost, kMaxTypeCost);
        ScaleCell(oil[t], cost, kMaxTypeCost);
        ScaleCell(buildTime[t], c.time.Structure(race, group), kMaxTime);
    }
}

void ScaleResearch() {
    const Config& c = config::g;
    uint16_t* gold = At<uint16_t>(kRvaUpgradeGold);
    uint16_t* lumber = At<uint16_t>(kRvaUpgradeLumber);
    uint16_t* oil = At<uint16_t>(kRvaUpgradeOil);
    uint8_t* time = At<uint8_t>(kRvaResearchTime);
    for (int i = 0; i < units::kResearchCount; ++i) {
        const units::ResearchInfo info = units::ResearchInfoOf(i);
        const double cost = c.costs.Research(info.race, info.group);
        ScaleCell(gold[i], cost, kMaxResearchCost);
        ScaleCell(lumber[i], cost, kMaxResearchCost);
        ScaleCell(oil[i], cost, kMaxResearchCost);
        ScaleCell(time[i], c.time.Research(info.race, info.group), kMaxTime);
    }
}

// [unit.<name>] / [building.<name>]: the player's own base numbers replace the game's before any multiplier.
// Sight is still a plain range 0..9 here: FinalizeTables turns it into a reveal-function pointer right after us,
// and 9 is the engine's maximum (there is no reveal function for more).
int ApplyUnitStats() {
    uint8_t* byteTables[kStatCount] = {};
    byteTables[kStatArmor] = At<uint8_t>(kRvaArmorByType);
    byteTables[kStatBasicDamage] = At<uint8_t>(kRvaBasicDamageByType);
    byteTables[kStatPiercingDamage] = At<uint8_t>(kRvaPiercingDamageByType);
    byteTables[kStatRange] = At<uint8_t>(kRvaAttackRangeByType);
    byteTables[kStatGold] = At<uint8_t>(kRvaGoldCostByType);
    byteTables[kStatLumber] = At<uint8_t>(kRvaLumberCostByType);
    byteTables[kStatOil] = At<uint8_t>(kRvaOilCostByType);
    byteTables[kStatBuildTime] = At<uint8_t>(kRvaBuildTimeByType);
    int changed = 0;
    for (int t = 0; t < units::kTypeCount; ++t)
        for (int stat = 0; stat < kStatCount; ++stat) {
            const int v = config::g.unitStat[t][stat];
            if (v < 0) continue;
            ++changed;
            if (stat == kStatHitPoints) At<uint16_t>(kRvaMaxHpByType)[t] = static_cast<uint16_t>(v);
            else if (stat == kStatSight) At<uint32_t>(kRvaSightByType)[t] = static_cast<uint32_t>(v);  // still a plain 0..9 here
            else if (stat >= kStatGold && stat <= kStatOil) byteTables[stat][t] = static_cast<uint8_t>(v / 10);
            else byteTables[stat][t] = static_cast<uint8_t>(v);
        }
    return changed;
}

void DescribeTree(const Multipliers& m, char* out, size_t size) {
    sprintf_s(out, size, "all x%.2f, human x%.2f, orc x%.2f", m.all, m.race[units::kHuman].all, m.race[units::kOrc].all);
}

bool g_rangePatchRefused = false;

}  // namespace

void OnNewMapTablesLoaded() {
    mod::EnsureConfigLoaded();
    // The tick-time multiplayer flag is not valid yet during a map load; this byte is what it is copied from.
    const bool multiplayer = *At<uint8_t>(kRvaNetGameAtLoad) != 0;
    SyncRangeBonus(multiplayer);
    spells::OnNewMap(multiplayer);
    upgrades::OnNewMap(multiplayer);
    if (multiplayer) {
        logx::Write("map load: multiplayer game, data tables left alone");
        return;
    }
    tweaks::OnNewMap();
    autocast::OnNewMap();
    resume::OnNewMap();
    trees::OnNewMap();
    aiwatch::OnNewMap();
    production::OnNewMap();  // the water / oil profile is counted again on the first pass of the new map
    const int statsSet = ApplyUnitStats();
    ScaleUnits();
    ScaleStructures();
    ScaleResearch();
    char health[96], costs[96], time[96];
    DescribeTree(config::g.health, health, sizeof(health));
    DescribeTree(config::g.costs, costs, sizeof(costs));
    DescribeTree(config::g.time, time, sizeof(time));
    logx::Write("map load: %d unit stats set, health [%s] costs [%s] time [%s] (group values on top)",
                statsSet, health, costs, time);
}

// Longbow / Lighter Axes: GetAttackRange (FUN_004ee660) ends its upgraded branch with `inc al` (FE C0). The same two
// bytes hold `add al, imm8` (04 nn), so the bonus becomes a number without moving any code. Both status panels print
// counter x the per-level byte at 0x8C11E4, which is kept in step so the UI shows the real bonus.
void SyncRangeBonus(bool multiplayer) {
    if (g_rangePatchRefused) return;
    uint8_t* insn = At<uint8_t>(kRvaRangeBonusInsn);
    const int desired = multiplayer ? 1 : config::g.rangeUpgradeBonus;
    int current;
    if (insn[0] == 0xFE && insn[1] == 0xC0) current = 1;
    else if (insn[0] == 0x04) current = insn[1];
    else {
        g_rangePatchRefused = true;
        logx::Write("range bonus instruction is %02X %02X, not the expected FE C0: [range] upgrade_bonus is ignored", insn[0], insn[1]);
        return;
    }
    if (current == desired) return;

    DWORD oldProtect;
    if (!VirtualProtect(insn, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (desired == 1) {
        insn[0] = 0xFE;
        insn[1] = 0xC0;
    } else {
        insn[0] = 0x04;
        insn[1] = static_cast<uint8_t>(desired);
    }
    VirtualProtect(insn, 2, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), insn, 2);
    *At<uint8_t>(kRvaRangeBonusDisplay) = static_cast<uint8_t>(desired);
    logx::Write("longbow / lighter axes range bonus set to +%d", desired);
}

}  // namespace datatweaks
