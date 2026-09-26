#include "datatweaks.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "log.h"
#include "mod.h"
#include "production.h"
#include "resume.h"
#include "dodge.h"
#include "scouts.h"
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

// The tables the new-map pass edits, as the game had them before the pass: the base a live reload starts from again.
// Sight is kept as the plain 0..9 the pass sees; after FinalizeTables the live table holds the matching pointers.
struct Snapshot {
    bool valid;
    uint16_t hp[units::kTypeCount];
    uint8_t gold[units::kTypeCount], lumber[units::kTypeCount], oil[units::kTypeCount], buildTime[units::kTypeCount];
    uint8_t armor[units::kTypeCount], basic[units::kTypeCount], piercing[units::kTypeCount], range[units::kTypeCount];
    uint8_t reactComputer[units::kTypeCount], reactHuman[units::kTypeCount];
    uint32_t sight[units::kTypeCount];
    uint16_t researchGold[units::kResearchCount], researchLumber[units::kResearchCount], researchOil[units::kResearchCount];
    uint8_t researchTime[units::kResearchCount];
};
Snapshot g_snap;
bool g_notNowLogged = false;
constexpr int kMaxSight = 9;  // the pointer table has 10 entries

template <typename T, size_t N>
void Copy(T (&to)[N], const T* from) { memcpy(to, from, sizeof(to)); }
template <typename T, size_t N>
void Put(T* to, const T (&from)[N]) { memcpy(to, from, sizeof(from)); }

void TakeSnapshot() {
    Copy(g_snap.hp, At<uint16_t>(kRvaMaxHpByType));
    Copy(g_snap.gold, At<uint8_t>(kRvaGoldCostByType));
    Copy(g_snap.lumber, At<uint8_t>(kRvaLumberCostByType));
    Copy(g_snap.oil, At<uint8_t>(kRvaOilCostByType));
    Copy(g_snap.buildTime, At<uint8_t>(kRvaBuildTimeByType));
    Copy(g_snap.armor, At<uint8_t>(kRvaArmorByType));
    Copy(g_snap.basic, At<uint8_t>(kRvaBasicDamageByType));
    Copy(g_snap.piercing, At<uint8_t>(kRvaPiercingDamageByType));
    Copy(g_snap.range, At<uint8_t>(kRvaAttackRangeByType));
    Copy(g_snap.reactComputer, At<uint8_t>(kRvaReactRangeComputer));
    Copy(g_snap.reactHuman, At<uint8_t>(kRvaReactRangeHuman));
    Copy(g_snap.sight, At<uint32_t>(kRvaSightByType));
    Copy(g_snap.researchGold, At<uint16_t>(kRvaUpgradeGold));
    Copy(g_snap.researchLumber, At<uint16_t>(kRvaUpgradeLumber));
    Copy(g_snap.researchOil, At<uint16_t>(kRvaUpgradeOil));
    Copy(g_snap.researchTime, At<uint8_t>(kRvaResearchTime));
    // A value outside 0..9 means this is not the raw table the loader leaves (it would be a pointer): no live reload.
    g_snap.valid = true;
    for (int t = 0; t < units::kTypeCount; ++t)
        if (g_snap.sight[t] > kMaxSight) g_snap.valid = false;
}

// Back to the snapshot. Sight goes in as the reveal-function pointer FinalizeTables would have made of it.
void RestoreSnapshot() {
    Put(At<uint16_t>(kRvaMaxHpByType), g_snap.hp);
    Put(At<uint8_t>(kRvaGoldCostByType), g_snap.gold);
    Put(At<uint8_t>(kRvaLumberCostByType), g_snap.lumber);
    Put(At<uint8_t>(kRvaOilCostByType), g_snap.oil);
    Put(At<uint8_t>(kRvaBuildTimeByType), g_snap.buildTime);
    Put(At<uint8_t>(kRvaArmorByType), g_snap.armor);
    Put(At<uint8_t>(kRvaBasicDamageByType), g_snap.basic);
    Put(At<uint8_t>(kRvaPiercingDamageByType), g_snap.piercing);
    Put(At<uint8_t>(kRvaAttackRangeByType), g_snap.range);
    Put(At<uint8_t>(kRvaReactRangeComputer), g_snap.reactComputer);
    Put(At<uint8_t>(kRvaReactRangeHuman), g_snap.reactHuman);
    uint32_t* sight = At<uint32_t>(kRvaSightByType);
    for (int t = 0; t < units::kTypeCount; ++t) sight[t] = At<uint32_t>(kRvaSightFunctions)[g_snap.sight[t]];
    Put(At<uint16_t>(kRvaUpgradeGold), g_snap.researchGold);
    Put(At<uint16_t>(kRvaUpgradeLumber), g_snap.researchLumber);
    Put(At<uint16_t>(kRvaUpgradeOil), g_snap.researchOil);
    Put(At<uint8_t>(kRvaResearchTime), g_snap.researchTime);
}

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
// `live`: the tables are already finalized, so a sight number is written as its reveal-function pointer.
int ApplyUnitStats(bool live) {
    uint8_t* byteTables[kStatCount] = {};
    byteTables[kStatArmor] = At<uint8_t>(kRvaArmorByType);
    byteTables[kStatBasicDamage] = At<uint8_t>(kRvaBasicDamageByType);
    byteTables[kStatPiercingDamage] = At<uint8_t>(kRvaPiercingDamageByType);
    byteTables[kStatRange] = At<uint8_t>(kRvaAttackRangeByType);
    byteTables[kStatGold] = At<uint8_t>(kRvaGoldCostByType);
    byteTables[kStatLumber] = At<uint8_t>(kRvaLumberCostByType);
    byteTables[kStatOil] = At<uint8_t>(kRvaOilCostByType);
    byteTables[kStatBuildTime] = At<uint8_t>(kRvaBuildTimeByType);
    uint8_t* reactComputer = At<uint8_t>(kRvaReactRangeComputer);
    uint8_t* reactHuman = At<uint8_t>(kRvaReactRangeHuman);
    int changed = 0;
    for (int t = 0; t < units::kTypeCount; ++t)
        for (int stat = 0; stat < kStatCount; ++stat) {
            const int v = config::g.unitStat[t][stat];
            if (v < 0) continue;
            ++changed;
            if (stat == kStatHitPoints) At<uint16_t>(kRvaMaxHpByType)[t] = static_cast<uint16_t>(v);
            else if (stat == kStatSight)  // a plain 0..9 at map load, the matching pointer once finalized
                At<uint32_t>(kRvaSightByType)[t] = live ? At<uint32_t>(kRvaSightFunctions)[v > kMaxSight ? kMaxSight : v]
                                                        : static_cast<uint32_t>(v);
            else if (stat >= kStatGold && stat <= kStatOil) byteTables[stat][t] = static_cast<uint8_t>(v / 10);
            else if (stat == kStatReactRange) reactComputer[t] = reactHuman[t] = static_cast<uint8_t>(v);
            else byteTables[stat][t] = static_cast<uint8_t>(v);
        }

    // A unit shoots only at a target it has already noticed, and how far it notices comes from these two tables
    // (FUN_004a8e00 at 0x4A8EB8 / 0x4A8ECB), not from the range table the panel prints. The game's own numbers leave
    // room above the attack range for most units (archer 4 / 7 / 5, juggernaught 6 / 10 / 8) but not for towers
    // (guard tower 6 / 6 / 6), so a raised `range` alone can leave a tower firing at its old distance. Lift the
    // react ranges to match; never lower them, and never touch a type that sets react_range itself.
    for (int t = 0; t < units::kTypeCount; ++t) {
        const int range = config::g.unitStat[t][kStatRange];
        if (range <= 0 || config::g.unitStat[t][kStatReactRange] >= 0) continue;
        char detail[96] = "";
        if (reactComputer[t] < range) {
            sprintf_s(detail, "computer %d -> %d", static_cast<int>(reactComputer[t]), range);
            reactComputer[t] = static_cast<uint8_t>(range);
        }
        if (reactHuman[t] < range) {
            char yours[48];
            sprintf_s(yours, "%syours %d -> %d", *detail ? " and " : "", static_cast<int>(reactHuman[t]), range);
            strcat_s(detail, yours);
            reactHuman[t] = static_cast<uint8_t>(range);
        }
        if (!*detail || live) continue;  // nothing raised, or a live reload (one summary line, not one per type)
        const units::Entry* unit = units::FindById(static_cast<uint8_t>(t));
        const units::Building* building = unit ? nullptr : units::FindBuildingById(static_cast<uint8_t>(t));
        char fallback[16];
        sprintf_s(fallback, "%d", t);
        logx::Write("%s %s: react range %s to match range %d", unit ? "unit" : (building ? "building" : "type"),
                    unit ? unit->name : (building ? building->name : fallback), detail, range);
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
        g_snap.valid = false;
        logx::Write("map load: multiplayer game, data tables left alone");
        return;
    }
    TakeSnapshot();
    g_notNowLogged = false;
    tweaks::OnNewMap();
    autocast::OnNewMap();
    scouts::OnNewMap();
    dodge::OnNewMap();
    resume::OnNewMap();
    trees::OnNewMap();
    production::OnNewMap();  // the water / oil profile is counted again on the first pass of the new map
    const int statsSet = ApplyUnitStats(false);
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

void OnConfigReloaded(bool multiplayer) {
    if (multiplayer) return;
    const bool fromSave = *At<uint16_t>(kRvaGameFromSave) != 0;
    if (!g_snap.valid || fromSave) {
        if (!g_notNowLogged)
            logx::Write("unit stats reload at the next new map (%s)",
                        fromSave ? "game loaded from a save" : "this game did not start in this session");
        g_notNowLogged = true;
        return;
    }
    uint16_t oldMax[units::kTypeCount];
    memcpy(oldMax, At<uint16_t>(kRvaMaxHpByType), sizeof oldMax);
    RestoreSnapshot();
    const int statsSet = ApplyUnitStats(true);
    ScaleUnits();
    ScaleStructures();
    ScaleResearch();

    // Units alive keep their share of the (new) maximum. A building under construction is left alone: its hit points
    // are the construction progress (FUN_004ed4e0), and they go on growing towards the new maximum.
    int rescaled = 0;
    World w;
    const uint16_t* newMax = At<uint16_t>(kRvaMaxHpByType);
    if (BuildWorld(w)) {
        for (unsigned i = 0; i < w.unitCount; ++i) {
            Unit* u = UnitAt(w, i);
            const uint8_t t = TypeOf(u);
            if (t >= units::kTypeCount || (Field<uint8_t>(u, kOffStateFlags) & 0x07) || oldMax[t] == newMax[t] || !oldMax[t]) continue;
            if ((w.typeFlags[t] & kTfBuilding) && !(Field<uint16_t>(u, kOffStateFlags) & kStateComplete)) continue;
            const int hp = Field<uint16_t>(u, kOffHp);
            if (!hp) continue;
            long long scaled = (static_cast<long long>(hp) * newMax[t] + oldMax[t] / 2) / oldMax[t];
            if (scaled < 1) scaled = 1;
            if (scaled > newMax[t]) scaled = newMax[t];
            Field<uint16_t>(u, kOffHp) = static_cast<uint16_t>(scaled);
            ++rescaled;
        }
    }
    char health[96], costs[96], time[96];
    DescribeTree(config::g.health, health, sizeof(health));
    DescribeTree(config::g.costs, costs, sizeof(costs));
    DescribeTree(config::g.time, time, sizeof(time));
    logx::Write("settings reloaded: unit stats applied again from this map's own tables: %d unit stats set, health [%s] "
                "costs [%s] time [%s], %d unit(s) kept their share of hit points",
                statsSet, health, costs, time, rescaled);
}

void ResetForTests() {
    g_snap.valid = false;
    g_notNowLogged = false;
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
