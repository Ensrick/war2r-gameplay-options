#include "datatweaks.h"

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

// Unit prices are bytes holding price / 10. A unit that cost something never becomes free.
void ScaleUnitCosts() {
    const double k = config::g.costUnits;
    if (k == 1.0) return;
    uint8_t* gold = At<uint8_t>(kRvaGoldCostByType);
    uint8_t* lumber = At<uint8_t>(kRvaLumberCostByType);
    for (int t = 0; t < units::kFirstBuilding; ++t)
        for (uint8_t* table : {gold, lumber}) {
            if (!table[t]) continue;
            int scaled = Scale(table[t], k);
            table[t] = static_cast<uint8_t>(scaled < 1 ? 1 : (scaled > kMaxUnitCost ? kMaxUnitCost : scaled));
        }
}

void ScaleUpgradeCosts(const uint8_t* indices, int count, double k) {
    if (k == 1.0) return;
    for (uint32_t rva : {kRvaUpgradeGold, kRvaUpgradeLumber, kRvaUpgradeOil}) {
        uint16_t* table = At<uint16_t>(rva);
        for (int i = 0; i < count; ++i) {
            const int scaled = Scale(table[indices[i]], k);
            table[indices[i]] = static_cast<uint16_t>(scaled > kMaxUpgradeCost ? kMaxUpgradeCost : scaled);
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
    // Arrows / throwing axes 1-2, ranger / berserker upgrade, longbow / lighter axes, scouting, marksmanship, regeneration.
    static const uint8_t kRangedUpgrades[] = {4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31};
    static const uint8_t kSiegeUpgrades[] = {20, 21, 22, 23};  // catapult 1-2, ballista 1-2
    ScaleHealth();
    ScaleUnitCosts();
    ScaleUpgradeCosts(kRangedUpgrades, sizeof(kRangedUpgrades), config::g.costRangedUpgrades);
    ScaleUpgradeCosts(kSiegeUpgrades, sizeof(kSiegeUpgrades), config::g.costSiegeUpgrades);
    AddSight();
    logx::Write("map load: health x%.2f units / x%.2f heroes, unit cost x%.2f, ranged upgrades x%.2f, siege upgrades x%.2f, "
                "sight bonuses applied",
                config::g.hpUnits, config::g.hpHeroes, config::g.costUnits, config::g.costRangedUpgrades,
                config::g.costSiegeUpgrades);
}

}  // namespace datatweaks
