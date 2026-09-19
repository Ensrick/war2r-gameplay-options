#pragma once
#include "config.h"
#include "world.h"

// [auto_production]: the local player's idle production buildings train units by themselves. Single player only (the
// tick's multiplayer gate); never a computer's building, never a research. Evidence: docs/research/production.md
namespace production {

enum Resource { kGold, kLumber, kOil };
enum Group { kGroupLand, kGroupNavy, kGroupNone };  // workers and tankers are in no group: they have their own rule

struct Price {
    int r[kResourceCount];
};

// ---- Decision core: plain numbers in, decisions out, no game memory (the selftest drives it directly) ----

Group GroupOf(int cls);
bool IsArmy(int cls);  // everything but workers and tankers

// Upgrade reserve per resource: the dearest purchasable item + extra x the sum of all the others.
Price Reserve(const Price* items, int count, double extra);
// Ships as a fraction of the army, from the map: the water fraction plus 5 % per oil source (at most 30 %), scaled by
// the hall tier (1.2 / 1.0 / 0.9) and navy_weight, capped at navy_max. A map with no oil and less than 10 % water is
// a land map (lakes): 0.
double NavyShare(int waterPercent, int oilSources, int tier, double navyWeight, int navyMax);
// Food: after the unit the mod is about to train, at least max(minFree, percent% of the supply) must still be free,
// so the player can still make peasants, transports or tankers himself. Units in training count as used (the game's
// own check forgets them); the supply counts as 200 at most, like every reader in the game.
bool FoodAllows(int supply, int used, int inTraining, int minFree, int percent);

struct Plan {
    Price bank{}, reserve{};
    int supply = 0, used = 0, inTraining = 0;
    int tier = 0;                              // best hall: 1 hall, 2 keep, 3 castle, 0 none
    double navyShare = 0;                      // 0..1 of the army, from the map profile
    int count[kProdClassCount] = {};           // own units, in training included
    bool trainable[kProdClassCount] = {};      // switched on, allowed by the map, requirements met, a building exists
    int levels[kProdClassCount] = {};          // upgrade levels of the class's line
    Price cost[kProdClassCount] = {};          // current price of the type the class trains for this player's race
};

// How many of the class the spare bank (bank minus upgrade reserve) pays for, by the tightest resource it costs.
double Buys(const Plan&, int cls);
// Affordable = the spare bank holds bank_multiple prices (twice that for submarines: a luxury, only when rich).
bool CanAfford(const Plan&, const AutoProduction&, int cls);
int ArmySize(const Plan&);  // units of the army classes (every one of them eats 1 food)
// Wanted number per army class: land and navy shares from the map, each mix renormalised over the classes that are
// available and affordable, bent by upgrades and by what the bank can pay for.
void Targets(const Plan&, const AutoProduction&, double target[kProdClassCount]);
// The army class among `candidates` (bit per class) with the largest target - count, or -1. *saving is set when a
// candidate was rejected only because it is already well over its share (the building keeps the money for others).
int PickArmyClass(const Plan&, const AutoProduction&, unsigned candidates, bool* saving);
// Nothing of the mix is affordable here: the class the bank buys most of, if that is at least filler_min units.
// This is what keeps a gold-rich, lumber-poor game producing (grunts at tier 3). A group the map gives no share of
// the army is skipped, so a land map never gets ships this way.
int PickFiller(const Plan&, const AutoProduction&, unsigned candidates);
bool WantWorker(const Plan&, const AutoProduction&);
bool WantTanker(const Plan&, const AutoProduction&, bool ownsOilPlatform);
bool FoodAllows(const Plan&, const AutoProduction&);
void Commit(Plan&, int cls);  // book a start: count, bank, food

// ---- Engine side ----
void OnNewMap();                                        // forget the cached map profile (water, oil, navy share)
void OnTick(const game::World& w, unsigned elapsedMs);  // one pass per second of play time
void Pass(const game::World& w, unsigned nowMs);        // one pass right now (tests); nowMs = play time for the back-off
unsigned StartCount();                                  // productions started since load (tests, log)
const Plan& LastPlan();                                 // the numbers of the last pass as it began (tests)

}  // namespace production
