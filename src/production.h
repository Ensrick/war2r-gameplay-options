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

// Something the player could pay for right now. A research may be the dearest item the reserve is built around; a
// BUILDING upgrade (keep, castle, guard / cannon tower) never is, because it is a choice he makes when he wants it,
// not a purchase already decided on. Early game the keep alone (2000 gold) would otherwise set the whole reserve.
struct Buy {
    Price price;
    bool anchor;
};
// Upgrade reserve per resource: the dearest anchor + extra x the sum of everything else (anchors and non-anchors).
Price Reserve(const Buy* items, int count, double extra);
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
    // Hard ceiling per class, -1 = none. The engine fills it from [auto_production.no_enemy_navy_cap] while no
    // hostile player owns a shipyard or a warship, and clears it the moment one does.
    int cap[kProdClassCount] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    bool ownShipyard = false;  // a finished shipyard of the player's
    bool enemyNavy = false;    // an enemy shipyard or warship on the map (HostileNavy)
    int shipFood = 1;          // food one warship eats (per-type counter table, 0 for a food-free type)
    int navyFood = 0;          // food land units must leave free for the navy, NavyFood() of this plan
};

// Below the class's ceiling (units in training included), or uncapped.
bool UnderCap(const Plan&, int cls);

// How many of the class the spare bank (bank minus upgrade reserve) pays for, by the tightest resource it costs.
double Buys(const Plan&, int cls);
// Affordable = the spare bank holds bank_multiple prices (twice that for submarines: a luxury, only when rich).
bool CanAfford(const Plan&, const AutoProduction&, int cls);
// The bank holds the plain price, upgrade reserve and bank_multiple ignored. This is the whole money rule for
// workers and the tanker (workers_ignore_reserve / tankers_ignore_reserve): they are the economy, not army shopping.
bool CanPay(const Plan&, int cls);
int ArmySize(const Plan&);  // units of the army classes (every one of them eats 1 food)
// Wanted number per army class: land and navy shares from the map, each mix renormalised over the classes that are
// available and affordable, bent by upgrades and by what the bank can pay for. A class the bank buys plenty_units
// or more of is at its full share: past that line the size of the bank plays no part in the mix at all.
void Targets(const Plan&, const AutoProduction&, double target[kProdClassCount]);
// The army class among `candidates` (bit per class) with the largest target - count, or -1. *saving is set when a
// candidate was rejected only because it is already well over its share (the building keeps the money for others).
int PickArmyClass(const Plan&, const AutoProduction&, unsigned candidates, bool* saving);
// Nothing of the mix is affordable here: the class the bank buys most of, if that is at least filler_min units.
// This is what keeps a gold-rich, lumber-poor game producing (grunts at tier 3). A group the map gives no share of
// the army is skipped, so a land map never gets ships this way.
int PickFiller(const Plan&, const AutoProduction&, unsigned candidates);
// Workers wanted at the player's best hall tier (workers_tier1 / 2 / 3); 0 without a hall.
int WorkerTarget(const Plan&, const AutoProduction&);
bool WantWorker(const Plan&, const AutoProduction&);
bool WantTanker(const Plan&, const AutoProduction&, bool ownsOilPlatform);
bool FoodAllows(const Plan&, const AutoProduction&);
// [auto_production] reserve_navy_food: with a finished shipyard of his own and an enemy navy on the map, the food
// the navy still needs to reach its share of the army: (round(navy share x (army + 1)) - warships) x food per ship,
// never more than is left under the 200 cap. 0 otherwise (setting off, no shipyard, no enemy navy, no warship class
// trainable). The share is the map's, not the money-weighted Targets(): waiting for oil is exactly when it matters.
// *have / *want (optional) get the warship count and the navy size it aims for.
int NavyFood(const Plan&, const AutoProduction&, int* have = nullptr, int* want = nullptr);
// A LAND class may start only if, after it, free food still covers the normal food_free rule plus plan.navyFood.
// Everything else (ships, workers, tankers) always passes.
bool NavyFoodAllows(const Plan&, const AutoProduction&, int cls);
void Commit(Plan&, int cls);  // book a start: count, bank, food

// ---- Saving up: a cheap class must not eat the resource a dearer one of the same building waits for ----
//
// PickArmyClass skips everything it cannot pay for, so the class furthest behind its share can starve for ever
// behind a cheaper one that spends the same scarce resource: a shipyard buys a destroyer the moment the oil passes
// 4 x 700 + reserve, the oil falls back, and the 4 x 1000 + reserve a battleship wants is never reached, however
// much of the mix belongs to battleships. When the wanted class is held back by MONEY ALONE (food, ceilings and
// prerequisites are other gates and none of this rule's business), the building keeps its money instead of spending
// that resource on a cheaper class. A class that does not cost the blocking
// resource is still built, so a gold-rich, lumber-poor game keeps making grunts while its knights wait, and land
// units never wait for oil.
struct SaveUp {
    int resource = -1;     // the resource being saved for, -1 = not saving
    int mark = 0;          // how much of it the bank held at the last check
    unsigned sinceMs = 0;  // play time when it last grew: the deadlock timer runs from here
};
struct Decision {
    int cls = -1;           // what to start at this building, -1 = nothing
    bool heldForNavy = false;  // a land unit was chosen but NavyFoodAllows said no
    int saveResource = -1;  // -1 = not saving; otherwise the resource, the class waited for and the numbers, for the log
    int saveClass = -1;
    int have = 0, need = 0;
};
// The resource that keeps `cls` out of CanAfford's reach (the tightest one, the same one Buys measures), or -1 when
// money is not what stops it.
int BlockingResource(const Plan&, const AutoProduction&, int cls);
// The whole decision for one idle building: the mix, the filler and the saving rule above. `state` is that building's
// own saving state (the engine keeps one per building slot), `nowMs` is play time. Pure but for `state`.
Decision Decide(const Plan&, const AutoProduction&, unsigned candidates, SaveUp& state, unsigned nowMs);

// ---- Engine side ----
void OnNewMap();                                        // forget the cached map profile (water, oil, navy share)
void OnTick(const game::World& w, unsigned elapsedMs);  // one pass per second of play time
void Pass(const game::World& w, unsigned nowMs);        // one pass right now (tests); nowMs = play time for the back-off
unsigned StartCount();                                  // productions started since load (tests, log)
// Landmass id of a tile (1..n, 0 = water / coast / off the map), as the last pass counted them (tests).
int LandmassAt(int x, int y);
const Plan& LastPlan();                                 // the numbers of the last pass as it began (tests)

}  // namespace production
