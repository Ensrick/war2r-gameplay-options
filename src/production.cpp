#include "production.h"

#include <cmath>
#include <cstring>

#include "log.h"

using namespace game;

namespace production {

// ---------------------------------------------------------------------------------------------------------------------
// Decision core
// ---------------------------------------------------------------------------------------------------------------------

Group GroupOf(int cls) {
    switch (cls) {
    case kProdInfantry: case kProdArchers: case kProdKnights: case kProdCasters: case kProdFlyers: case kProdSiege:
        return kGroupLand;
    case kProdDestroyers: case kProdBattleships: case kProdSubmarines:
        return kGroupNavy;
    default:
        return kGroupNone;  // workers, tankers
    }
}

bool IsArmy(int cls) { return GroupOf(cls) != kGroupNone; }

Price Reserve(const Price* items, int count, double extra) {
    Price out{};
    for (int r = 0; r < kResourceCount; ++r) {
        int dearest = 0;
        long long sum = 0;
        for (int i = 0; i < count; ++i) {
            sum += items[i].r[r];
            if (items[i].r[r] > dearest) dearest = items[i].r[r];
        }
        const double v = dearest + extra * static_cast<double>(sum - dearest);
        out.r[r] = v > 2e9 ? 2000000000 : static_cast<int>(std::ceil(v - 1e-9));
    }
    return out;
}

double NavyShare(int waterPercent, int oilSources, int tier, double navyWeight, int navyMax) {
    if (oilSources <= 0 && waterPercent < 10) return 0;  // lakes on a land map
    const double kTierFactor[kProdTiers] = {1.2, 1.0, 0.9};
    const int t = tier < 1 ? 1 : (tier > kProdTiers ? kProdTiers : tier);
    double oil = 0.05 * oilSources;
    if (oil > 0.30) oil = 0.30;
    double share = (waterPercent / 100.0 + oil) * kTierFactor[t - 1] * navyWeight;
    const double cap = navyMax / 100.0;
    if (share > cap) share = cap;
    return share < 0 ? 0 : share;
}

bool FoodAllows(int supply, int used, int inTraining, int minFree, int percent) {
    const int cap = supply > 200 ? 200 : supply;
    const int keep = (percent * cap + 99) / 100;  // percent of the supply, rounded up
    const int mustStayFree = minFree > keep ? minFree : keep;
    return cap - used - inTraining - 1 >= mustStayFree;  // - 1: the unit about to be trained
}

double Buys(const Plan& plan, int cls) {
    double n = 1e9;  // a unit that costs nothing is always affordable
    for (int r = 0; r < kResourceCount; ++r) {
        const int cost = plan.cost[cls].r[r];
        if (cost <= 0) continue;
        const double v = (static_cast<double>(plan.bank.r[r]) - plan.reserve.r[r]) / cost;
        if (v < n) n = v;
    }
    return n < 0 ? 0 : n;
}

bool CanAfford(const Plan& plan, const AutoProduction& cfg, int cls) {
    double want = cfg.classBankMultiple[cls] > 0 ? cfg.classBankMultiple[cls] : cfg.bankMultiple;
    if (cls == kProdSubmarines) want *= 2;  // only when there is money to spare
    return Buys(plan, cls) >= want;
}

int ArmySize(const Plan& plan) {
    int n = 0;
    for (int c = 0; c < kProdClassCount; ++c)
        if (IsArmy(c)) n += plan.count[c];
    return n;
}

void Targets(const Plan& plan, const AutoProduction& cfg, double target[kProdClassCount]) {
    const int tier = plan.tier < 1 ? 1 : (plan.tier > kProdTiers ? kProdTiers : plan.tier);
    double weight[kProdClassCount] = {};
    double groupTotal[2] = {0, 0};
    for (int c = 0; c < kProdClassCount; ++c) {
        const Group g = GroupOf(c);
        if (g == kGroupNone || !plan.trainable[c]) continue;
        const int share = g == kGroupNavy ? cfg.navy[tier - 1][c] : cfg.land[tier - 1][c];
        if (share <= 0) continue;
        double money = Buys(plan, c) / 5.0;
        if (money > 1) money = 1;
        weight[c] = share * (1.0 + cfg.upgradeBias * plan.levels[c]) * money;
        groupTotal[g] += weight[c];
    }
    // Submarines are a garnish: at most a fifth of the fleet, the rest goes to the other ships.
    const double others = groupTotal[kGroupNavy] - weight[kProdSubmarines];
    if (weight[kProdSubmarines] > 0.25 * others) {
        groupTotal[kGroupNavy] = others + 0.25 * others;
        weight[kProdSubmarines] = 0.25 * others;
    }
    const double navyShare = plan.navyShare < 0 ? 0 : (plan.navyShare > 1 ? 1 : plan.navyShare);
    const double groupShare[2] = {1.0 - navyShare, navyShare};
    double total = 0;
    for (int c = 0; c < kProdClassCount; ++c) {
        const Group g = GroupOf(c);
        if (g == kGroupNone || weight[c] <= 0) {
            weight[c] = 0;
            continue;
        }
        weight[c] = weight[c] / groupTotal[g] * groupShare[g];
        total += weight[c];
    }
    // One group empty (no shipyard, or no land building): the other takes the whole army.
    const double army = ArmySize(plan) + 1;
    for (int c = 0; c < kProdClassCount; ++c) target[c] = total > 0 ? weight[c] / total * army : 0;
}

int PickArmyClass(const Plan& plan, const AutoProduction& cfg, unsigned candidates, bool* saving) {
    if (saving) *saving = false;
    double target[kProdClassCount];
    Targets(plan, cfg, target);
    int best = -1;
    double bestDeficit = 0;
    for (int c = 0; c < kProdClassCount; ++c) {
        if (!(candidates & (1u << c)) || !IsArmy(c) || target[c] <= 0) continue;  // 0 = off in the mix, not trainable, broke
        if (!CanAfford(plan, cfg, c)) continue;
        const double deficit = target[c] - plan.count[c];
        // Battleships are listed after destroyers, so on a tie they win.
        if (best < 0 || deficit > bestDeficit + 1e-9 || (std::fabs(deficit - bestDeficit) <= 1e-9 && c == kProdBattleships)) {
            best = c;
            bestDeficit = deficit;
        }
    }
    if (best < 0) return -1;
    const double saveBelow = target[best] * 0.25 > 2.0 ? target[best] * 0.25 : 2.0;
    if (bestDeficit < -saveBelow) {  // well over its share: keep the money for the other classes
        if (saving) *saving = true;
        return -1;
    }
    return best;
}

int PickFiller(const Plan& plan, const AutoProduction& cfg, unsigned candidates) {
    int best = -1;
    double bestBuys = 0;
    for (int c = 0; c < kProdClassCount; ++c) {
        if (!(candidates & (1u << c)) || !IsArmy(c) || !plan.trainable[c]) continue;
        // The map decides land against ships; only the mix inside a group is overridden here. No ships on a land map.
        if (GroupOf(c) == kGroupNavy ? plan.navyShare <= 0 : plan.navyShare >= 1) continue;
        if (!CanAfford(plan, cfg, c)) continue;
        const double buys = Buys(plan, c);
        if (buys > bestBuys) {
            bestBuys = buys;
            best = c;
        }
    }
    return bestBuys >= cfg.fillerMin ? best : -1;
}

bool FoodAllows(const Plan& plan, const AutoProduction& cfg) {
    return FoodAllows(plan.supply, plan.used, plan.inTraining, cfg.foodFreeMin, cfg.foodFreePercent);
}

bool WantWorker(const Plan& plan, const AutoProduction& cfg) {
    return plan.trainable[kProdWorkers] && plan.count[kProdWorkers] < cfg.workersPerHallTier * plan.tier &&
           FoodAllows(plan, cfg) && CanAfford(plan, cfg, kProdWorkers);
}

// One tanker, once an oil platform is his: enough to keep the oil coming without a fleet of them.
bool WantTanker(const Plan& plan, const AutoProduction& cfg, bool ownsOilPlatform) {
    return ownsOilPlatform && plan.trainable[kProdTankers] && plan.count[kProdTankers] == 0 && FoodAllows(plan, cfg) &&
           CanAfford(plan, cfg, kProdTankers);
}

void Commit(Plan& plan, int cls) {
    ++plan.count[cls];
    ++plan.inTraining;
    for (int r = 0; r < kResourceCount; ++r) plan.bank.r[r] -= plan.cost[cls].r[r];
}

// ---------------------------------------------------------------------------------------------------------------------
// Engine side
// ---------------------------------------------------------------------------------------------------------------------

namespace {

constexpr unsigned kPassEveryMs = 1000;
constexpr unsigned kBackoffMs = 10000;
constexpr uint16_t kJobProducing = 0x10;  // kOffJobFlags
constexpr uint8_t kNotTrainable = 'n';    // 0x838248 entry of a type no building trains

unsigned g_playMs = 0;
unsigned g_sincePassMs = 0;
unsigned g_starts = 0;
Plan g_lastPlan;

// The map decides how much of the army is ships. Counted once per map: every tile and every oil source.
struct MapProfile {
    bool valid;
    uint32_t signature;
    int waterPercent;
    int oilSources;
};
MapProfile g_map;

// Per unit slot, verified by creation serial (the unit struct has nothing for this).
constexpr unsigned kMaxSlots = 2048;
struct SlotState {
    uint32_t serial;
    unsigned backoffUntilMs;
    bool pending;          // we started a unit here and have not seen the building idle since
    uint8_t pendingType;
    uint32_t serialMark;   // highest creation serial alive when it started
};
SlotState g_slots[kMaxSlots];

// Unit type -> class, -1 for everything the mod never builds (transports, flying machines / zeppelins, sappers,
// heroes, attack peasants ...).
int ClassOfType(uint8_t type) {
    switch (type) {
    case 0x02: case 0x03: return kProdWorkers;
    case 0x00: case 0x01: return kProdInfantry;
    case 0x08: case 0x09: case 0x12: case 0x13: return kProdArchers;
    case 0x06: case 0x07: case 0x0C: case 0x0D: return kProdKnights;
    case 0x0A: case 0x0B: return kProdCasters;
    case 0x2A: case 0x2B: return kProdFlyers;
    case 0x04: case 0x05: return kProdSiege;
    case 0x1A: case 0x1B: return kProdTankers;
    case 0x1E: case 0x1F: return kProdDestroyers;
    case 0x20: case 0x21: return kProdBattleships;
    case 0x26: case 0x27: return kProdSubmarines;
    default: return -1;
    }
}

// Classes a building type trains (halls: workers; shipyards: tankers besides the three warship classes).
unsigned ClassesAt(uint8_t buildingType) {
    switch (buildingType) {
    case 0x3C: case 0x3D: return 1u << kProdInfantry | 1u << kProdArchers | 1u << kProdKnights | 1u << kProdSiege;
    case 0x46: case 0x47: return 1u << kProdFlyers;
    case 0x50: case 0x51: return 1u << kProdCasters;
    case 0x48: case 0x49: return 1u << kProdTankers | 1u << kProdDestroyers | 1u << kProdBattleships | 1u << kProdSubmarines;
    case 0x4A: case 0x4B: case 0x58: case 0x59: case 0x5A: case 0x5B: return 1u << kProdWorkers;
    default: return 0;
    }
}

// A keep or castle trains what its town hall trains (FUN_004ac840 maps 0x58 / 0x5A to 0x4A, 0x59 / 0x5B to 0x4B).
uint8_t AsTrainer(uint8_t buildingType) {
    if (buildingType == 0x58 || buildingType == 0x5A) return 0x4A;
    if (buildingType == 0x59 || buildingType == 0x5B) return 0x4B;
    return buildingType;
}

// What the player has, for the requirement rules. Buildings are counted complete and alive, like the game's counters.
struct Owned {
    int buildings[units::kTypeCount];
    // Buildings of that type that are not already paying for a research or a building upgrade (job kind 1, 2 or 3).
    // The research buttons want an idle building (research_at, docs/research/production.md); one that is TRAINING is
    // counted anyway, because the unit it is making has nothing to do with the upgrade money the reserve protects.
    int freeForResearch[units::kTypeCount];
    uint8_t player;
    bool paladins;     // paladin / ogre-mage upgrade known (0x919250 & 0x100000)
    int rangerLevel;   // 0x918C6C: ranger / berserker upgrade done
    int Pair(int humanType) const { return buildings[humanType] + buildings[humanType + 1]; }
};

// The type a class trains for a race (0 human, 1 orc).
uint8_t TypeFor(int cls, int race, const Owned& o) {
    switch (cls) {
    case kProdWorkers: return static_cast<uint8_t>(0x02 + race);
    case kProdInfantry: return static_cast<uint8_t>(0x00 + race);
    case kProdArchers: return static_cast<uint8_t>((o.rangerLevel ? 0x12 : 0x08) + race);
    case kProdKnights: return static_cast<uint8_t>((o.paladins ? 0x0C : 0x06) + race);
    case kProdCasters: return static_cast<uint8_t>(0x0A + race);
    case kProdFlyers: return static_cast<uint8_t>(0x2A + race);
    case kProdSiege: return static_cast<uint8_t>(0x04 + race);
    case kProdTankers: return static_cast<uint8_t>(0x1A + race);
    case kProdDestroyers: return static_cast<uint8_t>(0x1E + race);
    case kProdSubmarines: return static_cast<uint8_t>(0x26 + race);
    default: return static_cast<uint8_t>(0x20 + race);  // battleships
    }
}

// ALOW "units allowed" bit per trainable type: what the train buttons test (FUN_004e3b00 footman 0x1, 4E3A30 peasant
// 0x2, 4E3B30 siege 0x4, 4E3B80 / 4E3BE0 knight, paladin 0x8, 4E3A60 / 4E3AB0 archer, ranger 0x10, 4E3C40 caster
// 0x20, 4E3C70 tanker 0x40, 4E3CA0 destroyer 0x80, 4E3D20 battleship 0x200, 4E3D70 submarine 0x400, 4E3E60 gryphon /
// dragon 0x1000). 0 for everything else, so a type the mod never builds can never slip through.
uint32_t AllowBit(uint8_t type) {
    switch (type & ~1u) {
    case 0x00: return 0x1;
    case 0x02: return 0x2;
    case 0x04: return 0x4;
    case 0x06: case 0x0C: return 0x8;
    case 0x08: case 0x12: return 0x10;
    case 0x0A: return 0x20;
    case 0x1A: return 0x40;
    case 0x1E: return 0x80;
    case 0x20: return 0x200;
    case 0x26: return 0x400;
    case 0x2A: return 0x1000;
    default: return 0;
    }
}

// Mirror of the requirement functions 0x8C0428[type] (the selftest checks every table entry is the function mirrored):
// 4A1F70 none; 4AC6C0 blacksmith + lumber mill; 4AC6F0 / 4AC730 blacksmith + stables, paladin upgrade not done / done;
// 4AC770 / 4AC7A0 lumber mill, ranger upgrade not done / done; 4AC7D0 inventor / alchemist; 4AC7F0 foundry.
bool RequirementsMet(uint8_t type, const Owned& o) {
    switch (type & ~1u) {
    case 0x04: return o.Pair(0x52) && o.Pair(0x4C);
    case 0x06: return o.Pair(0x52) && o.Pair(0x42) && !o.paladins;
    case 0x0C: return o.Pair(0x52) && o.Pair(0x42) && o.paladins;
    case 0x08: return o.Pair(0x4C) && !o.rangerLevel;
    case 0x12: return o.Pair(0x4C) && o.rangerLevel;
    case 0x20: return o.Pair(0x4E) != 0;
    case 0x26: return o.Pair(0x44) != 0;
    case 0x00: case 0x02: case 0x0A: case 0x1A: case 0x1E: case 0x2A: return true;
    default: return false;
    }
}

// Can this type be trained at all right now: allowed by the map, requirements met, a building that trains it exists.
// StartProduction itself checks NONE of this (docs/research/production.md section 1): a mission that forbids a unit
// would happily build it and the mod must be the one to refuse. The mask is read fresh every pass, so it follows a
// new map, a loaded savegame and anything that changes it while the mission runs.
bool CanTrain(uint8_t type, const Owned& o) {
    const uint8_t at = At<uint8_t>(kRvaTrainedAt)[type];
    if (at == kNotTrainable || !AllowBit(type)) return false;
    if (!(At<uint32_t>(kRvaUnitsAllowed)[o.player] & AllowBit(type)) || !RequirementsMet(type, o)) return false;
    int trainers = o.buildings[at];
    if (at == 0x4A) trainers += o.buildings[0x58] + o.buildings[0x5A];
    if (at == 0x4B) trainers += o.buildings[0x59] + o.buildings[0x5B];
    return trainers > 0;
}

// The live price, so [costs], [unit.<name>] and a map's own UDTA all count.
Price UnitPrice(uint8_t type) {
    return {{At<uint8_t>(kRvaGoldCostByType)[type] * 10, At<uint8_t>(kRvaLumberCostByType)[type] * 10,
             At<uint8_t>(kRvaOilCostByType)[type] * 10}};
}

// ---- the upgrade reserve: everything the player could buy right now ----

// Weapon / armor pairs 0..23, four ids per line (human pair, orc pair): level counter offset from 0x918BEC and the
// ALOW bit of level 1 (level 2 is the next bit). From the research buttons: 4E31F0 swords 4 << a (melee counter),
// 4E3180 arrows 1 << a (missile), 4E3400 shields 0x10 << a, 4E3470 ship cannons 0x40 << a, 4E34E0 ship armor
// 0x100 << a, 4E3240 siege 0x1000 << a.
constexpr int kPairLine[6] = {0x10, 0x00, 0x30, 0x40, 0x50, 0x70};
constexpr uint32_t kPairFlag[6] = {0x4, 0x1, 0x10, 0x40, 0x100, 0x1000};
// Ranger line 24..27 / berserker 28..31: ranger (needs a keep or castle, 4E3290), longbow, scouting, marksmanship
// (need the ranger upgrade: 4E32F0, 4E3340, 4E3390).
constexpr int kRangerLine[4] = {0x80, 0x90, 0xA0, 0xB0};
constexpr uint32_t kRangerFlag[4] = {0x10000, 0x20000, 0x40000, 0x80000};
// Spell research 32..51 (UGRD flag column, data_tables.md): the "known" bit of each. 34 holy vision, 38 fireball,
// 43 eye of kilrogg and 46 death coil have no research button (0 here).
constexpr uint32_t kSpellFlag[20] = {0x100000, 0x100000, 0, 0x2, 0x8, 0x10, 0, 0x40, 0x80, 0x100,
                                     0x200,    0,        0x800, 0x2000, 0, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000};

uint8_t Level(uint8_t player, int line) { return At<uint8_t>(kRvaUpgradeLevels + line)[player]; }

bool ResearchPurchasable(int id, const Owned& o) {
    const uint8_t p = o.player;
    // No building of the type that researches it, or the only one is already paying for another research.
    if (!o.freeForResearch[At<uint8_t>(kRvaResearchAt)[id]]) return false;
    if (id < 24) {
        const uint32_t flag = kPairFlag[id / 4] << (id & 1);
        return (At<uint32_t>(kRvaUpgradesAllowed)[p] & flag) && !(At<uint32_t>(kRvaUpgradesInResearch)[p] & flag) &&
               Level(p, kPairLine[id / 4]) == (id & 1);
    }
    if (id < 32) {
        const int k = (id - 24) % 4;
        const uint32_t flag = kRangerFlag[k];
        if (!(At<uint32_t>(kRvaUpgradesAllowed)[p] & flag) || (At<uint32_t>(kRvaUpgradesInResearch)[p] & flag)) return false;
        if (k == 0) return !o.rangerLevel && (o.Pair(0x58) || o.Pair(0x5A));
        return o.rangerLevel && !Level(p, kRangerLine[k]);
    }
    const uint32_t flag = kSpellFlag[id - 32];
    if (!flag) return false;
    const uint32_t known = At<uint32_t>(kRvaSpellsResearched)[p];
    if (!(At<uint32_t>(kRvaSpellsAllowed)[p] & flag) || (known & flag) || (At<uint32_t>(kRvaSpellsInResearch)[p] & flag)) return false;
    const bool needsUpgrade = id == 35 || id == 36 || id == 44 || id == 50;  // healing, exorcism, bloodlust, runes (4E3920)
    return !needsUpgrade || (known & 0x100000);
}

// Items for the reserve. Building upgrades count once per building that could take one: keep on a hall (ALOW 0x8000000,
// barracks), castle on a keep (ALOW 0x10000000, stables, lumber mill, blacksmith: 4E36F0), and on a scout tower the
// dearer of guard tower (lumber mill) / cannon tower (blacksmith) (4E37B0). A building already doing one is skipped.
// The keep / castle ALOW bits are the same units-allowed mask a unit is checked against; the tower button (4E37B0)
// tests no ALOW bit at all in this build, only the lumber mill / blacksmith counter, so neither does this.
int Purchasable(const World& w, const Owned& o, Price* out, int max) {
    int n = 0;
    for (int id = 0; id < units::kResearchCount && n < max; ++id)
        if (ResearchPurchasable(id, o))
            out[n++] = {{At<uint16_t>(kRvaUpgradeGold)[id], At<uint16_t>(kRvaUpgradeLumber)[id], At<uint16_t>(kRvaUpgradeOil)[id]}};
    const uint32_t allowed = At<uint32_t>(kRvaUnitsAllowed)[o.player];
    for (unsigned i = 0; i < w.unitCount && n < max; ++i) {
        Unit* b = UnitAt(w, i);
        if (OwnerOf(b) != o.player || !IsActive(b) || !(Field<uint16_t>(b, kOffStateFlags) & kStateComplete)) continue;
        if ((Field<uint16_t>(b, kOffJobFlags) & kJobProducing) && Field<uint8_t>(b, kOffJobKind) == 3) continue;
        const uint8_t t = TypeOf(b), race = t & 1;
        int target = -1;
        if ((t == 0x4A || t == 0x4B) && (allowed & 0x8000000) && o.Pair(0x3C)) target = 0x58 + race;
        else if ((t == 0x58 || t == 0x59) && (allowed & 0x10000000) && o.Pair(0x42) && o.Pair(0x4C) && o.Pair(0x52)) target = 0x5A + race;
        else if (t == 0x40 || t == 0x41) {
            if (o.Pair(0x52)) target = 0x62 + race;       // cannon tower, the dearer one
            else if (o.Pair(0x4C)) target = 0x60 + race;  // guard tower
        }
        if (target >= 0) out[n++] = UnitPrice(static_cast<uint8_t>(target));
    }
    return n;
}

int Levels(int cls, const Owned& o, int race) {
    const uint8_t p = o.player;
    switch (cls) {
    case kProdInfantry: return Level(p, 0x10) + Level(p, 0x30);
    case kProdKnights: return Level(p, 0x10) + Level(p, 0x30) + (o.paladins ? 1 : 0);
    case kProdArchers: return Level(p, 0x00) + Level(p, 0x80) + Level(p, 0x90) + Level(p, 0xA0) + Level(p, 0xB0);
    case kProdSiege: return Level(p, 0x70);
    case kProdDestroyers:
    case kProdBattleships:
    case kProdSubmarines: return Level(p, 0x40) + Level(p, 0x50);
    case kProdCasters: {  // researched spells of the race's caster: mage tower 37 39-42 / temple 45 47-49 51
        uint32_t mask = race ? 0xBA000u : 0x3D0u;
        uint32_t known = At<uint32_t>(kRvaSpellsResearched)[p] & mask;
        int n = 0;
        for (; known; known &= known - 1) ++n;
        return n;
    }
    default: return 0;
    }
}

bool Selected(Unit* b) {
    if (*At<Unit*>(kRvaSelectedUnit) == b) return true;
    Unit* const* list = At<Unit*>(kRvaSelection);
    for (int i = 0; i < 12; ++i)
        if (list[i] == b) return true;
    return false;
}

// Water tiles and oil sources of the running map. Counted once: the square flags never change for water, and oil
// patches only turn into platforms (both count). The signature notices a new map or a loaded game.
void UpdateMapProfile(const World& w) {
    const uint16_t* square = *At<uint16_t*>(kRvaSquareFlags);
    if (!square) return;
    uint32_t signature = static_cast<uint32_t>(w.mapSize) * 2654435761u;
    signature ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(square));
    for (int i = 0; i < w.mapSize * w.mapSize; i += 257) signature = signature * 31u + square[i];
    if (g_map.valid && g_map.signature == signature) return;

    int water = 0;
    const int tiles = w.mapSize * w.mapSize;
    for (int i = 0; i < tiles; ++i)
        if (square[i] & kSqWater) ++water;
    int oil = 0;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (!IsActive(u)) continue;
        const uint8_t t = TypeOf(u);
        if (t == kTypeOilPatch || (w.typeFlags[t] & kTfOilPlatform)) ++oil;
    }
    g_map.valid = true;
    g_map.signature = signature;
    g_map.waterPercent = tiles > 0 ? water * 100 / tiles : 0;
    g_map.oilSources = oil;
    const AutoProduction& cfg = config::g.production;
    logx::Write("production: map is %d %% water with %d oil source(s): ships get %d / %d / %d %% of the army by hall tier",
                g_map.waterPercent, g_map.oilSources,
                static_cast<int>(NavyShare(g_map.waterPercent, g_map.oilSources, 1, cfg.navyWeight, cfg.navyMax) * 100 + 0.5),
                static_cast<int>(NavyShare(g_map.waterPercent, g_map.oilSources, 2, cfg.navyWeight, cfg.navyMax) * 100 + 0.5),
                static_cast<int>(NavyShare(g_map.waterPercent, g_map.oilSources, 3, cfg.navyWeight, cfg.navyMax) * 100 + 0.5));
}

const char* const kClassNames[kProdClassCount] = {"worker",    "infantry",   "archer",      "knight",    "caster", "flyer",
                                                  "siege",     "tanker",     "destroyer",   "battleship", "submarine"};

bool Start(const World& w, Plan& plan, Unit* b, unsigned slot, int cls, uint8_t type, uint32_t serialMark, unsigned nowMs) {
    using StartProductionFn = int(__cdecl*)(Unit*, uint8_t, uint8_t);
    SlotState& s = g_slots[slot];
    if (!reinterpret_cast<StartProductionFn>(g_base + kRvaStartProduction)(b, type, 0)) {
        s.backoffUntilMs = nowMs + kBackoffMs;
        if (config::g.logCasts) logx::Write("production: the game refused %s type 0x%02X at building type 0x%02X, waiting 10 s", kClassNames[cls], type, TypeOf(b));
        return false;
    }
    Commit(plan, cls);
    ++g_starts;
    s.pending = true;
    s.pendingType = type;
    s.serialMark = serialMark;
    if (config::g.logCasts)
        logx::Write("production: %s type 0x%02X at building type 0x%02X (%d,%d), bank %d/%d/%d after", kClassNames[cls], type,
                    TypeOf(b), Field<int16_t>(b, kOffX), Field<int16_t>(b, kOffY), At<int32_t>(kRvaPlayerGold)[w.localPlayer],
                    At<int32_t>(kRvaPlayerLumber)[w.localPlayer], At<int32_t>(kRvaPlayerOil)[w.localPlayer]);
    return true;
}

}  // namespace

void OnNewMap() { g_map.valid = false; }

void Pass(const World& w, unsigned nowMs) {
    const AutoProduction& cfg = config::g.production;
    const uint8_t p = w.localPlayer;
    static Owned o;
    memset(&o, 0, sizeof(o));
    o.player = p;
    o.paladins = (At<uint32_t>(kRvaSpellsResearched)[p] & 0x100000) != 0;
    o.rangerLevel = Level(p, 0x80);

    Plan plan;
    uint32_t maxSerial = 0, newestOwn[0x3A] = {};
    bool ownsPlatform = false;
    int race = -1;
    struct Idle {
        Unit* unit;
        unsigned slot;
    };
    static Idle idle[kMaxSlots];
    int idleCount = 0;
    const unsigned count = w.unitCount < kMaxSlots ? w.unitCount : kMaxSlots;
    for (unsigned i = 0; i < count; ++i) {
        Unit* u = UnitAt(w, i);
        const uint32_t serial = Field<uint32_t>(u, kOffSerial);
        if (g_slots[i].serial != serial) g_slots[i] = SlotState{serial, 0, false, 0, 0};
        if (!IsActive(u)) continue;
        if (serial > maxSerial) maxSerial = serial;
        if (OwnerOf(u) != p) continue;
        const uint8_t type = TypeOf(u);
        if (!(w.typeFlags[type] & kTfBuilding)) {
            if (type < 0x3A && serial > newestOwn[type]) newestOwn[type] = serial;
            const int cls = ClassOfType(type);
            if (cls >= 0) ++plan.count[cls];
            continue;
        }
        if (!(Field<uint16_t>(u, kOffStateFlags) & kStateComplete)) continue;
        ++o.buildings[type];
        const bool busy = (Field<uint16_t>(u, kOffJobFlags) & kJobProducing) != 0;
        if (!busy || Field<uint8_t>(u, kOffJobKind) == 0) ++o.freeForResearch[type];
        if (w.typeFlags[type] & kTfOilPlatform) ownsPlatform = true;
        if (!ClassesAt(type)) continue;
        if (race < 0) race = type & 1;
        if (busy) {
            if (Field<uint8_t>(u, kOffJobKind) == 0) {
                const int cls = ClassOfType(Field<uint8_t>(u, kOffJobId));
                if (cls >= 0) ++plan.count[cls];
            }
        } else {
            idle[idleCount++] = {u, i};
        }
    }
    if (race < 0) return;  // no production building at all
    UpdateMapProfile(w);

    // A unit we started finished (or was refunded): no new unit of its type means it could not be placed. Wait.
    for (int k = 0; k < idleCount; ++k) {
        SlotState& s = g_slots[idle[k].slot];
        if (!s.pending) continue;
        s.pending = false;
        if (s.pendingType >= 0x3A || newestOwn[s.pendingType] <= s.serialMark) s.backoffUntilMs = nowMs + kBackoffMs;
    }

    plan.bank = {{At<int32_t>(kRvaPlayerGold)[p], At<int32_t>(kRvaPlayerLumber)[p], At<int32_t>(kRvaPlayerOil)[p]}};
    plan.supply = At<uint16_t>(kRvaFoodSupply)[p];
    plan.used = At<uint16_t>(kRvaUnitsCounted)[p] - At<uint16_t>(kRvaFoodFreeUnits)[p];
    plan.inTraining = At<uint16_t>(kRvaUnitsInTraining)[p];
    plan.tier = o.Pair(0x5A) ? 3 : (o.Pair(0x58) ? 2 : (o.Pair(0x4A) ? 1 : 0));
    plan.navyShare = NavyShare(g_map.waterPercent, g_map.oilSources, plan.tier, cfg.navyWeight, cfg.navyMax);
    for (int c = 0; c < kProdClassCount; ++c) {
        const uint8_t type = TypeFor(c, race, o);
        plan.cost[c] = UnitPrice(type);
        plan.trainable[c] = cfg.unitClass[c] && CanTrain(type, o);
        plan.levels[c] = Levels(c, o, race);
    }
    static Price items[256];
    const int itemCount = Purchasable(w, o, items, 256);
    plan.reserve = Reserve(items, itemCount, cfg.reserveExtra);
    g_lastPlan = plan;

    auto usable = [&](const Idle& b) { return nowMs >= g_slots[b.slot].backoffUntilMs && !Selected(b.unit); };

    // 1. Workers: every idle hall, keep or castle while below workers_per_hall_tier x the best tier.
    for (int k = 0; k < idleCount; ++k) {
        const uint8_t t = TypeOf(idle[k].unit);
        if (!(ClassesAt(t) & (1u << kProdWorkers)) || !usable(idle[k]) || !WantWorker(plan, cfg)) continue;
        if (Start(w, plan, idle[k].unit, idle[k].slot, kProdWorkers, TypeFor(kProdWorkers, t & 1, o), maxSerial, nowMs)) idle[k].unit = nullptr;
    }
    // 2. One tanker, once an oil platform is yours.
    for (int k = 0; k < idleCount; ++k) {
        if (!idle[k].unit) continue;
        const uint8_t t = TypeOf(idle[k].unit);
        if (!(ClassesAt(t) & (1u << kProdTankers)) || !usable(idle[k]) || !WantTanker(plan, cfg, ownsPlatform)) continue;
        if (Start(w, plan, idle[k].unit, idle[k].slot, kProdTankers, TypeFor(kProdTankers, t & 1, o), maxSerial, nowMs)) {
            idle[k].unit = nullptr;
            break;
        }
    }
    // 3. The army, from every idle production building in the same pass.
    for (int k = 0; k < idleCount; ++k) {
        if (!idle[k].unit || !usable(idle[k])) continue;
        if (!FoodAllows(plan, cfg)) break;
        const uint8_t t = TypeOf(idle[k].unit);
        unsigned candidates = 0;
        for (int c = 0; c < kProdClassCount; ++c)
            if ((ClassesAt(t) & (1u << c)) && IsArmy(c) && plan.trainable[c]) candidates |= 1u << c;
        if (!candidates) continue;
        bool saving = false;
        int cls = PickArmyClass(plan, cfg, candidates, &saving);
        // Nothing of the mix affordable (but not "saving up"): build whatever the bank is full of.
        if (cls < 0 && !saving) cls = PickFiller(plan, cfg, candidates);
        if (cls < 0) continue;
        const uint8_t type = TypeFor(cls, t & 1, o);
        if (At<uint8_t>(kRvaTrainedAt)[type] != AsTrainer(t)) continue;  // the game's table disagrees: leave it alone
        Start(w, plan, idle[k].unit, idle[k].slot, cls, type, maxSerial, nowMs);
    }
}

void OnTick(const World& w, unsigned elapsedMs) {
    g_playMs += elapsedMs;
    if (!config::g.production.enabled) {
        g_sincePassMs = 0;
        return;
    }
    g_sincePassMs += elapsedMs;
    if (g_sincePassMs < kPassEveryMs) return;
    g_sincePassMs = 0;
    Pass(w, g_playMs);
}

unsigned StartCount() { return g_starts; }
const Plan& LastPlan() { return g_lastPlan; }

}  // namespace production
