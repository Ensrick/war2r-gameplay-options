#include "datatweaks.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "config.h"
#include "log.h"
#include "mod.h"
#include "units.h"
#include "world.h"

using namespace game;

namespace datatweaks {

namespace {

constexpr int kTypeCount = 110;
// What the engine can store. HP is an unsigned 16-bit word in the type table and in the unit, and the damage code
// (FUN_004bd8f0) compares and subtracts it unsigned; every direct read of unit+0x22 in the exe is a movzx.
// Cosmetic only: the status panel stops printing HP numbers from 10000 up.
constexpr int kMaxHp = 65535;
constexpr int kMaxUnitCost = 255;      // unit prices are a BYTE holding price / 10, so 2550 is the most a unit can cost
constexpr int kMaxUpgradeCost = 65535;  // upgrade prices are plain 16-bit words

int Scale(int value, double factor) { return static_cast<int>(value * factor + 0.5); }

void ScaleHealth() {
    const Config& c = config::g;
    if (c.hpUnits == 1.0 && c.hpHeroes == 1.0) return;
    uint16_t* hp = At<uint16_t>(kRvaMaxHpByType);
    for (int t = 0; t < units::kFirstBuilding; ++t) {  // units only: structures keep the game's numbers
        if (!hp[t]) continue;
        const double k = c.isHero[t] ? c.hpHeroes : c.hpUnits;
        int scaled = Scale(hp[t], k);
        if (scaled < 1) scaled = 1;
        hp[t] = static_cast<uint16_t>(scaled > kMaxHp ? kMaxHp : scaled);
    }
}

// Unit and structure prices are bytes holding price / 10. Something that cost anything never becomes free.
void ScaleTypeCost(int type, double k, bool withOil) {
    if (k == 1.0) return;
    uint8_t* tables[3] = {At<uint8_t>(kRvaGoldCostByType), At<uint8_t>(kRvaLumberCostByType), At<uint8_t>(kRvaOilCostByType)};
    for (int r = 0; r < (withOil ? 3 : 2); ++r) {
        uint8_t& cell = tables[r][type];
        if (!cell) continue;
        const int scaled = Scale(cell, k);
        cell = static_cast<uint8_t>(scaled < 1 ? 1 : (scaled > kMaxUnitCost ? kMaxUnitCost : scaled));
    }
}

// A building upgrade (town hall -> keep, scout tower -> guard tower ...) is priced as the unit type it turns into:
// the pay path FUN_004ac610 reads the same per-type tables for training, placement and building upgrades.
bool IsBuildingUpgradeTarget(int type) { return (type >= 0x58 && type <= 0x5B) || (type >= 0x60 && type <= 0x63); }

void ScaleTypeCosts() {
    const Config& c = config::g;
    for (int t = 0; t < kTypeCount; ++t) {
        if (t < units::kFirstBuilding) ScaleTypeCost(t, c.cost[kCostUnits], false);  // units: gold and lumber, as asked for
        else ScaleTypeCost(t, c.cost[IsBuildingUpgradeTarget(t) ? kCostBuildingUpgrades : kCostBuildings], true);
    }
}

// Upgrade and spell research prices: 16-bit words in PUD UGRD order (index list: docs/research/data_tables.md).
void ScaleUpgradeCosts() {
    static const struct {
        CostGroup group;
        uint8_t indices[12];
        int count;
    } kGroups[] = {
        {kCostMeleeUpgrades, {0, 1, 2, 3, 8, 9, 10, 11}, 8},                        // swords, battle axes, human / orc shields
        {kCostRangedUpgrades, {4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31}, 12},    // arrows, axes, ranger / berserker line
        {kCostSiegeUpgrades, {20, 21, 22, 23}, 4},                                  // catapult 1-2, ballista 1-2
        {kCostPaladinOgreMage, {32, 33, 34, 35, 36, 43, 44, 50}, 8},                // both upgrades, holy vision, healing, exorcism, eye, bloodlust, runes
        {kCostNavalUpgrades, {12, 13, 14, 15, 16, 17, 18, 19}, 8},                  // ship cannons and armor, both races
        {kCostMageDeathKnightSpells, {37, 38, 39, 40, 41, 42, 45, 46, 47, 48, 49, 51}, 12},
    };
    for (const auto& g : kGroups) {
        const double k = config::g.cost[g.group];
        if (k == 1.0) continue;
        for (uint32_t rva : {kRvaUpgradeGold, kRvaUpgradeLumber, kRvaUpgradeOil}) {
            uint16_t* table = At<uint16_t>(rva);
            for (int i = 0; i < g.count; ++i) {
                uint16_t& cell = table[g.indices[i]];
                if (!cell) continue;
                const int scaled = Scale(cell, k);
                cell = static_cast<uint16_t>(scaled < 1 ? 1 : (scaled > kMaxUpgradeCost ? kMaxUpgradeCost : scaled));
            }
        }
    }
}

// At this point the sight table still holds plain ranges 0..9 (FinalizeTables turns them into function pointers
// right after us). 9 is the engine's maximum: there is no reveal function for more.
void AddSight() {
    uint32_t* sight = At<uint32_t>(kRvaSightByType);
    for (int t = 0; t < kTypeCount; ++t) {
        if (!config::g.sightBonus[t] || sight[t] > 9) continue;
        const uint32_t boosted = sight[t] + config::g.sightBonus[t];
        sight[t] = boosted > 9 ? 9 : boosted;
    }
}

}  // namespace

void OnNewMapTablesLoaded() {
    mod::EnsureConfigLoaded();
    // The tick-time multiplayer flag is not valid yet during a map load; this byte is what it is copied from.
    if (*At<uint8_t>(kRvaNetGameAtLoad) != 0) {
        logx::Write("map load: multiplayer game, data tables left alone");
        return;
    }
    ScaleHealth();
    ScaleTypeCosts();
    ScaleUpgradeCosts();
    AddSight();
    char costs[256] = "";
    for (int i = 0; i < kCostGroupCount; ++i) {
        char item[64];
        sprintf_s(item, " %s x%.2f", config::kCostKeys[i], config::g.cost[i]);
        strcat_s(costs, item);
    }
    logx::Write("map load: health units x%.2f heroes x%.2f; prices:%s; sight bonuses applied", config::g.hpUnits,
                config::g.hpHeroes, costs);
}

}  // namespace datatweaks
