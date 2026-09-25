#include "production.h"

#include <cmath>
#include <cstdio>
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

Price Reserve(const Buy* items, int count, double extra) {
    Price out{};
    for (int r = 0; r < kResourceCount; ++r) {
        int dearest = 0;
        long long sum = 0;
        for (int i = 0; i < count; ++i) {
            sum += items[i].price.r[r];
            if (items[i].anchor && items[i].price.r[r] > dearest) dearest = items[i].price.r[r];
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

// How many of its own price the spare bank must hold before this class is affordable.
static double BankMultipleFor(const AutoProduction& cfg, int cls) {
    const double want = cfg.classBankMultiple[cls] > 0 ? cfg.classBankMultiple[cls] : cfg.bankMultiple;
    return cls == kProdSubmarines ? want * 2 : want;  // only when there is money to spare
}

bool CanAfford(const Plan& plan, const AutoProduction& cfg, int cls) {
    return Buys(plan, cls) >= BankMultipleFor(cfg, cls);
}

bool CanPay(const Plan& plan, int cls) {
    for (int r = 0; r < kResourceCount; ++r)
        if (plan.bank.r[r] < plan.cost[cls].r[r]) return false;
    return true;
}

bool UnderCap(const Plan& plan, int cls) { return plan.cap[cls] < 0 || plan.count[cls] < plan.cap[cls]; }

int ArmySize(const Plan& plan) {
    int n = 0;
    for (int c = 0; c < kProdClassCount; ++c)
        if (IsArmy(c)) n += plan.count[c];
    return n;
}

void Targets(const Plan& plan, const AutoProduction& cfg, double target[kProdClassCount]) {
    const int tier = plan.tier < 1 ? 1 : (plan.tier > kProdTiers ? kProdTiers : plan.tier);
    // Being able to buy plenty_units of a class is as good as being able to buy a thousand: above that line money
    // stops counting and the mix is the configured shares alone. Below it a class's share shrinks in proportion to
    // what the bank buys of it, so a resource you have run out of moves the army towards what you can still pay for.
    const double plenty = cfg.plentyUnits > 0 ? cfg.plentyUnits : 1;
    double weight[kProdClassCount] = {};
    double groupTotal[2] = {0, 0};
    for (int c = 0; c < kProdClassCount; ++c) {
        const Group g = GroupOf(c);
        // A class at its ceiling drops out and its share goes to the others; when every ship class is capped out,
        // the navy group is empty and the whole army becomes land units.
        if (g == kGroupNone || !plan.trainable[c] || !UnderCap(plan, c)) continue;
        const int share = g == kGroupNavy ? cfg.navy[tier - 1][c] : cfg.land[tier - 1][c];
        if (share <= 0) continue;
        double money = Buys(plan, c) / plenty;
        if (money > 1) money = 1;
        const double bias = cfg.classUpgradeBias[c] >= 0 ? cfg.classUpgradeBias[c] : cfg.upgradeBias;
        weight[c] = share * (1.0 + bias * plan.levels[c]) * money;
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
        if (!(candidates & (1u << c)) || !IsArmy(c) || !plan.trainable[c] || !UnderCap(plan, c)) continue;
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

int NavyFood(const Plan& plan, const AutoProduction& cfg, int* have, int* want) {
    int ships = 0;
    for (int c = 0; c < kProdClassCount; ++c)
        if (GroupOf(c) == kGroupNavy) ships += plan.count[c];
    if (have) *have = ships;
    if (want) *want = 0;
    if (!cfg.reserveNavyFood || !plan.ownShipyard || !plan.enemyNavy || plan.navyShare <= 0) return 0;
    const int tier = plan.tier < 1 ? 1 : (plan.tier > kProdTiers ? kProdTiers : plan.tier);
    bool buildable = false;  // some warship class the mix asks for can still be trained
    for (int c = 0; c < kProdClassCount; ++c)
        if (GroupOf(c) == kGroupNavy && plan.trainable[c] && UnderCap(plan, c) && cfg.navy[tier - 1][c] > 0) buildable = true;
    if (!buildable) return 0;
    const double share = plan.navyShare > 1 ? 1 : plan.navyShare;
    const int size = static_cast<int>(std::floor(share * (ArmySize(plan) + 1) + 0.5));
    if (want) *want = size;
    const int missing = size - ships;
    if (missing <= 0) return 0;
    int food = missing * plan.shipFood;
    const int room = 200 - plan.used - plan.inTraining;  // the navy can never grow past the 200 cap
    if (food > room) food = room;
    return food < 0 ? 0 : food;
}

bool NavyFoodAllows(const Plan& plan, const AutoProduction& cfg, int cls) {
    if (GroupOf(cls) != kGroupLand || plan.navyFood <= 0) return true;
    return FoodAllows(plan.supply, plan.used, plan.inTraining + plan.navyFood, cfg.foodFreeMin, cfg.foodFreePercent);
}

// Workers are a plain count target, not army shopping: with workers_ignore_reserve they wait for nothing but the
// price itself. Making the economy wait for the upgrade reserve is what stalls a poor start (the keep upgrade alone
// is 2000 gold, more than a hall full of peasants).
int WorkerTarget(const Plan& plan, const AutoProduction& cfg) {
    if (plan.tier < 1) return 0;  // no hall: nothing trains workers anyway
    return cfg.workersTier[plan.tier > kProdTiers ? kProdTiers - 1 : plan.tier - 1];
}

bool WantWorker(const Plan& plan, const AutoProduction& cfg) {
    return plan.trainable[kProdWorkers] && plan.count[kProdWorkers] < WorkerTarget(plan, cfg) && UnderCap(plan, kProdWorkers) &&
           FoodAllows(plan, cfg) &&
           (cfg.workersIgnoreReserve ? CanPay(plan, kProdWorkers) : CanAfford(plan, cfg, kProdWorkers));
}

// One tanker, once an oil platform is his: enough to keep the oil coming without a fleet of them. It pays for itself,
// so by default it waits for the price only.
bool WantTanker(const Plan& plan, const AutoProduction& cfg, bool ownsOilPlatform) {
    return ownsOilPlatform && plan.trainable[kProdTankers] && plan.count[kProdTankers] == 0 && UnderCap(plan, kProdTankers) &&
           FoodAllows(plan, cfg) &&
           (cfg.tankersIgnoreReserve ? CanPay(plan, kProdTankers) : CanAfford(plan, cfg, kProdTankers));
}

void Commit(Plan& plan, int cls) {
    ++plan.count[cls];
    ++plan.inTraining;
    for (int r = 0; r < kResourceCount; ++r) plan.bank.r[r] -= plan.cost[cls].r[r];
}

int BlockingResource(const Plan& plan, const AutoProduction& cfg, int cls) {
    if (CanAfford(plan, cfg, cls)) return -1;  // money is not what stops it
    int worst = -1;
    double tightest = 0;
    for (int r = 0; r < kResourceCount; ++r) {
        const int cost = plan.cost[cls].r[r];
        if (cost <= 0) continue;
        const double v = (static_cast<double>(plan.bank.r[r]) - plan.reserve.r[r]) / cost;
        if (worst < 0 || v < tightest) {
            worst = r;
            tightest = v;
        }
    }
    return worst;  // the resource Buys measured: what the class is actually waiting for
}

// What the bank must hold of one resource before CanAfford lets a class through: the upgrade reserve plus
// bank_multiple prices. The number the log line prints as "need".
static int NeededFor(const Plan& plan, const AutoProduction& cfg, int cls, int resource) {
    const double v = plan.reserve.r[resource] + BankMultipleFor(cfg, cls) * plan.cost[cls].r[resource];
    return v > 2e9 ? 2000000000 : static_cast<int>(std::ceil(v - 1e-9));
}

static Decision DecideMix(const Plan& plan, const AutoProduction& cfg, unsigned candidates, SaveUp& state, unsigned nowMs);

Decision Decide(const Plan& plan, const AutoProduction& cfg, unsigned candidates, SaveUp& state, unsigned nowMs) {
    Decision d = DecideMix(plan, cfg, candidates, state, nowMs);
    if (d.cls >= 0 && !NavyFoodAllows(plan, cfg, d.cls)) {  // a land unit would eat the food the ships still need
        d.cls = -1;
        d.heldForNavy = true;
    }
    return d;
}

static Decision DecideMix(const Plan& plan, const AutoProduction& cfg, unsigned candidates, SaveUp& state, unsigned nowMs) {
    Decision d;
    if (!FoodAllows(plan, cfg)) {  // nothing is anybody's fault here: the food rule stops the whole building
        state.resource = -1;
        return d;
    }
    bool overShare = false;
    d.cls = PickArmyClass(plan, cfg, candidates, &overShare);
    // Nothing of the mix affordable (but not "well over its share"): build whatever the bank is full of.
    if (d.cls < 0 && !overShare) d.cls = PickFiller(plan, cfg, candidates);
    if (cfg.saveUpSeconds <= 0) {  // saving switched off: exactly what the mod did before the rule existed
        state.resource = -1;
        return d;
    }

    // The class furthest behind its share, whatever it costs. A target of 0 already covers "switched off", "not
    // trainable" and "at its ceiling", and the food rule was handled above: what is left can only be money.
    double target[kProdClassCount];
    Targets(plan, cfg, target);
    int wanted = -1;
    double bestDeficit = 0;
    for (int c = 0; c < kProdClassCount; ++c) {
        if (!(candidates & (1u << c)) || !IsArmy(c) || target[c] <= 0) continue;
        const double deficit = target[c] - plan.count[c];
        if (wanted < 0 || deficit > bestDeficit) {
            wanted = c;
            bestDeficit = deficit;
        }
    }
    const int resource = (wanted >= 0 && bestDeficit > 0) ? BlockingResource(plan, cfg, wanted) : -1;
    // Nothing is waiting, or what this building would build does not touch what it waits for: carry on.
    if (resource < 0 || d.cls < 0 || plan.cost[d.cls].r[resource] <= 0) {
        state.resource = -1;
        return d;
    }

    // Keep the money, but never for ever: the timer runs from the last time the blocked resource grew. Spending it
    // yourself is not growth, it only moves the mark, so a resource that goes nowhere still releases on time.
    const int have = plan.bank.r[resource];
    if (state.resource != resource) {
        state.resource = resource;
        state.mark = have;
        state.sinceMs = nowMs;
    } else if (have > state.mark) {
        state.mark = have;
        state.sinceMs = nowMs;
    } else if (have < state.mark) {
        state.mark = have;
    }
    if (nowMs - state.sinceMs >= static_cast<unsigned>(cfg.saveUpSeconds) * 1000u) {
        state.resource = -1;  // it is not coming: build once and start over
        return d;
    }
    d.saveResource = resource;
    d.saveClass = wanted;
    d.have = have;
    d.need = NeededFor(plan, cfg, wanted, resource);
    d.cls = -1;
    return d;
}

// ---------------------------------------------------------------------------------------------------------------------
// Engine side
// ---------------------------------------------------------------------------------------------------------------------

namespace {

constexpr unsigned kPassEveryMs = 1000;
constexpr unsigned kBackoffMs = 10000;
constexpr unsigned kDiagEveryMs = 30000;  // at most one "why nothing" line per 30 s of play
constexpr unsigned kSaveLogEveryMs = 60000;  // and one "saving" line per 60 s, however many buildings are saving
constexpr uint16_t kJobProducing = 0x10;  // kOffJobFlags
constexpr uint8_t kNotTrainable = 'n';    // 0x838248 entry of a type no building trains

unsigned g_playMs = 0;
unsigned g_sincePassMs = 0;
unsigned g_starts = 0;
unsigned g_nextDiagMs = 0;
unsigned g_nextSaveLogMs = 0;
int g_navyCapState = -1;  // -1 not decided yet, 0 the caps are in force, 1 the enemy has a navy
int g_navyFoodLogged = 0;  // the navy food reserve the log last announced
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
    SaveUp save;           // what this building is saving up for, and since when
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
// Why a class is not being trained, for the log line. The order is the order the gates are applied in.
enum Block {
    kBlockNone, kBlockOff, kBlockNever, kBlockNoBuilding, kBlockMission, kBlockPrereq, kBlockNoPlatform, kBlockCap, kBlockBusy,
    kBlockWaiting, kBlockFood, kBlockNavyFood, kBlockGold, kBlockLumber, kBlockOil, kBlockReserve, kBlockBank, kBlockSaving,
    kBlockEnough
};
const char* const kBlockNames[] = {"ok",   "off",     "never",  "no building", "mission", "prereq",  "no platform", "cap", "busy",
                                   "waiting", "food", "navy food", "gold", "lumber",   "oil",     "reserve", "bank",
                                   "saving",  "enough"};
const char* const kResourceNames[kResourceCount] = {"gold", "lumber", "oil"};

// The building comes first, so a class the player has nothing to build in stays out of the log line entirely.
Block TrainBlock(uint8_t type, const Owned& o) {
    const uint8_t at = At<uint8_t>(kRvaTrainedAt)[type];
    if (at == kNotTrainable || !AllowBit(type)) return kBlockNever;
    int trainers = o.buildings[at];
    if (at == 0x4A) trainers += o.buildings[0x58] + o.buildings[0x5A];
    if (at == 0x4B) trainers += o.buildings[0x59] + o.buildings[0x5B];
    if (!trainers) return kBlockNoBuilding;
    if (!(At<uint32_t>(kRvaUnitsAllowed)[o.player] & AllowBit(type))) return kBlockMission;
    return RequirementsMet(type, o) ? kBlockNone : kBlockPrereq;
}

bool CanTrain(uint8_t type, const Owned& o) { return TrainBlock(type, o) == kBlockNone; }

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
int Purchasable(const World& w, const Owned& o, Buy* out, int max) {
    int n = 0;
    for (int id = 0; id < units::kResearchCount && n < max; ++id)
        if (ResearchPurchasable(id, o))
            out[n++] = {{{At<uint16_t>(kRvaUpgradeGold)[id], At<uint16_t>(kRvaUpgradeLumber)[id],
                          At<uint16_t>(kRvaUpgradeOil)[id]}},
                        true};  // a research is money he has already decided to spend: it may anchor the reserve
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
        if (target >= 0) out[n++] = {UnitPrice(static_cast<uint8_t>(target)), false};  // his choice, never the anchor
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
    // Water only: the other flag bits move while the game runs (a felled or regrown tree, a new building), and a
    // changed signature would recount the map and log the profile line again.
    for (int i = 0; i < w.mapSize * w.mapSize; i += 257) signature = signature * 31u + (square[i] & kSqWater);
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

// A hostile shipyard (complete or half built) or warship anywhere on the map. Fog does not come into it: this is the
// unit array, not what the player can see, which is what the author asked for. Own and allied ships never count.
bool HostileNavy(const World& w, Unit* u, uint8_t me) {
    if (OwnerOf(u) == me || !IsEnemy(w, me, u)) return false;
    const uint8_t t = TypeOf(u);
    return t == 0x48 || t == 0x49                      // shipyard
           || (t >= 0x1E && t <= 0x21)                 // destroyer, battleship / juggernaught
           || t == 0x26 || t == 0x27;                  // submarine / turtle
}

// Food one unit of the type eats: 1, or 0 when CountAdd files it under the food-free counter (skeletons, daemons,
// critters). The pointer table is static .data, relocated with the image like every absolute address.
int ShipFood(uint8_t type) {
    return At<uint32_t>(kRvaCounterByType)[type] == static_cast<uint32_t>(g_base + kRvaFoodFreeUnits) ? 0 : 1;
}

// One line when the navy food reserve changes, never a line per pass.
void LogNavyFood(int food, int have, int want) {
    if (food == g_navyFoodLogged) return;
    g_navyFoodLogged = food;
    if (food > 0)
        logx::Write("production: keeping %d food for ships (navy %d of %d)", food, have, want);
    else
        logx::Write("production: no food kept for ships any more (navy %d of %d)", have, want);
}

// One line when the ship caps come into force and one when they lift, never a line per pass.
void LogNavyCaps(const AutoProduction& cfg, bool enemyNavy) {
    const int state = enemyNavy ? 1 : 0;
    if (g_navyCapState == state) return;
    g_navyCapState = state;
    if (enemyNavy) {
        logx::Write("production: an enemy shipyard or warship is on the map: the ship caps are off");
        return;
    }
    char n[4][8];
    const int cls[4] = {kProdTankers, kProdDestroyers, kProdBattleships, kProdSubmarines};
    for (int i = 0; i < 4; ++i) {
        if (cfg.noEnemyNavyCap[cls[i]] < 0) strcpy_s(n[i], "any");
        else sprintf_s(n[i], "%d", cfg.noEnemyNavyCap[cls[i]]);
    }
    logx::Write("production: no enemy shipyard: ships capped at %s tanker / %s destroyers / %s battleships / %s submarines",
                n[0], n[1], n[2], n[3]);
}

// The first gate that stops a class this pass, for the log line below.
Block WhyNot(const Plan& plan, const AutoProduction& cfg, const Owned& o, int race, int cls, unsigned idleMask,
             unsigned usableMask, bool ownsPlatform, unsigned savingResources) {
    if (!cfg.unitClass[cls]) return kBlockOff;
    const Block b = TrainBlock(TypeFor(cls, race, o), o);
    if (b != kBlockNone) return b;
    if (cls == kProdTankers && !ownsPlatform) return kBlockNoPlatform;
    if (!UnderCap(plan, cls)) return kBlockCap;  // the enemy has no navy and we hold enough of this class
    if (!(usableMask & (1u << cls))) return (idleMask & (1u << cls)) ? kBlockWaiting : kBlockBusy;
    if (!FoodAllows(plan, cfg)) return kBlockFood;
    if (!NavyFoodAllows(plan, cfg, cls)) return kBlockNavyFood;  // the food the ships still need
    for (int r = 0; r < kResourceCount; ++r)
        if (plan.bank.r[r] < plan.cost[cls].r[r]) return static_cast<Block>(kBlockGold + r);
    const bool ignoresReserve = (cls == kProdWorkers && cfg.workersIgnoreReserve) ||
                                (cls == kProdTankers && cfg.tankersIgnoreReserve);
    if (!ignoresReserve) {
        if (Buys(plan, cls) < 1) return kBlockReserve;    // the bank holds the price, the upgrade reserve does not
        if (!CanAfford(plan, cfg, cls)) return kBlockBank;  // affordable, but not bank_multiple times over
    }
    // A building is saving that resource up for a class further behind its share: this one would have spent it.
    for (int r = 0; r < kResourceCount; ++r)
        if ((savingResources & (1u << r)) && plan.cost[cls].r[r] > 0) return kBlockSaving;
    return kBlockEnough;  // nothing stops it: the count target or the mix says there are enough already
}

// Why the pass produced nothing, with the numbers behind it. One line per 30 s of play, and only while
// [general] log_casts is on: enough to answer "why is it not building anything?" without guessing.
void LogNothing(const Plan& plan, const AutoProduction& cfg, const Owned& o, int race, unsigned idleMask,
                unsigned usableMask, bool ownsPlatform, unsigned savingResources, unsigned nowMs) {
    if (!config::g.logCasts || nowMs < g_nextDiagMs) return;
    g_nextDiagMs = nowMs + kDiagEveryMs;
    double target[kProdClassCount];
    Targets(plan, cfg, target);
    char blocked[400] = "";
    size_t used = 0;
    for (int c = 0; c < kProdClassCount; ++c) {
        const Block b = WhyNot(plan, cfg, o, race, c, idleMask, usableMask, ownsPlatform, savingResources);
        if (b == kBlockNever || b == kBlockNoBuilding) continue;  // nothing of the kind anywhere: not news
        char one[64];
        if (b == kBlockEnough && IsArmy(c))
            sprintf_s(one, "%s%s=enough(%+.1f)", used ? ", " : "", config::kProductionClassKeys[c], target[c] - plan.count[c]);
        else
            sprintf_s(one, "%s%s=%s", used ? ", " : "", config::kProductionClassKeys[c], kBlockNames[b]);
        if (used + strlen(one) >= sizeof(blocked)) break;
        strcat_s(blocked, one);
        used = strlen(blocked);
    }
    const int cap = plan.supply > 200 ? 200 : plan.supply;
    logx::Write("production: nothing (workers %d/%d, food free %d, gold %d lum %d oil %d, reserve %d/%d/%d, blocked: %s)",
                plan.count[kProdWorkers], WorkerTarget(plan, cfg), cap - plan.used - plan.inTraining,
                plan.bank.r[kGold], plan.bank.r[kLumber], plan.bank.r[kOil], plan.reserve.r[kGold],
                plan.reserve.r[kLumber], plan.reserve.r[kOil], blocked);
}

// One line when a building starts keeping its money, at most one per 60 s of play and only with log_casts on.
void LogSaving(const Decision& d, unsigned nowMs) {
    if (!config::g.logCasts || nowMs < g_nextSaveLogMs) return;
    g_nextSaveLogMs = nowMs + kSaveLogEveryMs;
    logx::Write("production: saving %s for %s (have %d, need %d)", kResourceNames[d.saveResource], kClassNames[d.saveClass],
                d.have, d.need);
}

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

void OnNewMap() {
    g_map.valid = false;
    g_nextDiagMs = 0;
    g_nextSaveLogMs = 0;
    g_navyCapState = -1;
    g_navyFoodLogged = 0;
    for (unsigned i = 0; i < kMaxSlots; ++i) g_slots[i].save = SaveUp{};  // no building saves into the next map
}

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
    bool ownsPlatform = false, enemyNavy = false;
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
        if (g_slots[i].serial != serial) g_slots[i] = SlotState{serial, 0, false, 0, 0, SaveUp{}};
        // A unit INSIDE a building (a tanker in its platform, a peasant in the mine or handing in at the hall, anyone in
        // a transport) is hidden, not gone: it is the player's unit and must be counted, or the mod trains a second
        // tanker every time the first one is loading. The engine's own counters use the same filter (state & 7).
        const uint8_t state = Field<uint8_t>(u, kOffStateFlags) & 0x0F;
        if (state & kStateGoneMask) continue;  // free slot, dying, dead
        const bool hidden = (state & kStateHidden) != 0;
        if (serial > maxSerial) maxSerial = serial;
        if (OwnerOf(u) != p) {
            if (!hidden && !enemyNavy && HostileNavy(w, u, p)) enemyNavy = true;
            continue;
        }
        const uint8_t type = TypeOf(u);
        if (hidden && (w.typeFlags[type] & kTfBuilding)) continue;  // no such thing in this game: never a production site
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
    LogNavyCaps(cfg, enemyNavy);
    for (int c = 0; c < kProdClassCount; ++c) {
        const uint8_t type = TypeFor(c, race, o);
        plan.cost[c] = UnitPrice(type);
        plan.trainable[c] = cfg.unitClass[c] && CanTrain(type, o);
        plan.levels[c] = Levels(c, o, race);
        plan.cap[c] = enemyNavy ? -1 : cfg.noEnemyNavyCap[c];
    }
    plan.ownShipyard = o.Pair(0x48) > 0;
    plan.enemyNavy = enemyNavy;
    plan.shipFood = 0;
    for (int c = 0; c < kProdClassCount; ++c)
        if (GroupOf(c) == kGroupNavy) {
            const int food = ShipFood(TypeFor(c, race, o));
            if (food > plan.shipFood) plan.shipFood = food;
        }
    int navyHave = 0, navyWant = 0;
    plan.navyFood = NavyFood(plan, cfg, &navyHave, &navyWant);
    LogNavyFood(plan.navyFood, navyHave, navyWant);
    static Buy items[256];
    const int itemCount = Purchasable(w, o, items, 256);
    plan.reserve = Reserve(items, itemCount, cfg.reserveExtra);
    g_lastPlan = plan;

    auto usable = [&](const Idle& b) { return nowMs >= g_slots[b.slot].backoffUntilMs && !Selected(b.unit); };

    unsigned idleMask = 0, usableMask = 0;  // classes with an idle building, and with one the mod may use right now
    for (int k = 0; k < idleCount; ++k) {
        const unsigned classes = ClassesAt(TypeOf(idle[k].unit));
        idleMask |= classes;
        if (usable(idle[k])) usableMask |= classes;
    }
    const unsigned startsBefore = g_starts;

    // 1. Workers: every idle hall, keep or castle while below the best tier's workers_tierN.
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
    unsigned savingResources = 0;  // resources a building is keeping back this pass, for the diagnostic line
    for (int k = 0; k < idleCount; ++k) {
        if (!idle[k].unit || !usable(idle[k])) continue;
        if (!FoodAllows(plan, cfg)) break;
        const uint8_t t = TypeOf(idle[k].unit);
        unsigned candidates = 0;
        for (int c = 0; c < kProdClassCount; ++c)
            if ((ClassesAt(t) & (1u << c)) && IsArmy(c) && plan.trainable[c]) candidates |= 1u << c;
        if (!candidates) continue;
        SaveUp& save = g_slots[idle[k].slot].save;
        const int savedBefore = save.resource;
        plan.navyFood = NavyFood(plan, cfg);  // a ship or a land unit started this pass moves it
        const Decision d = Decide(plan, cfg, candidates, save, nowMs);
        if (d.saveResource >= 0) {
            savingResources |= 1u << d.saveResource;
            if (savedBefore != d.saveResource) LogSaving(d, nowMs);  // only when it STARTS saving
        }
        if (d.cls < 0) continue;
        const uint8_t type = TypeFor(d.cls, t & 1, o);
        if (At<uint8_t>(kRvaTrainedAt)[type] != AsTrainer(t)) continue;  // the game's table disagrees: leave it alone
        Start(w, plan, idle[k].unit, idle[k].slot, d.cls, type, maxSerial, nowMs);
    }
    // A building was idle and nothing was built: say why, once every 30 s.
    if (g_starts == startsBefore && idleCount > 0)
        LogNothing(plan, cfg, o, race, idleMask, usableMask, ownsPlatform, savingResources, nowMs);
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
