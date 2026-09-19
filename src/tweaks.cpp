#include "tweaks.h"

#include "config.h"
#include "log.h"

using namespace game;

namespace tweaks {

namespace {

unsigned g_regenAccumMs = 0;

// [heroes] regen and [unit_regen]: hit points per second of play, paced by the time the simulation was actually
// stepping. A hero follows [heroes] while that is on (and covers him), everything else that is not a structure follows
// [unit_regen]: land units, flyers, ships, the computer's too unless regen_for = "mine". The two never add up.
void Regenerate(const World& w, unsigned elapsedMs) {
    const int heroRate = config::g.heroRegen ? config::g.heroRegenPerSecond : 0;
    const int unitRate = config::g.unitRegen ? config::g.unitRegenPerSecond : 0;
    if (heroRate <= 0 && unitRate <= 0) {
        g_regenAccumMs = 0;
        return;
    }
    g_regenAccumMs += elapsedMs;
    if (g_regenAccumMs < 1000) return;
    const int seconds = static_cast<int>(g_regenAccumMs / 1000);
    g_regenAccumMs %= 1000;

    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (!IsActive(u)) continue;
        const uint8_t type = TypeOf(u);
        const bool mine = OwnerOf(u) == w.localPlayer;
        int rate = 0;
        if (heroRate > 0 && config::g.isHero[type] && (mine || !config::g.heroRegenMineOnly)) rate = heroRate;
        else if (unitRate > 0 && !(w.typeFlags[type] & kTfBuilding) && (mine || !config::g.unitRegenMineOnly)) rate = unitRate;
        if (rate <= 0) continue;
        const int hp = Field<uint16_t>(u, kOffHp), maxHp = MaxHp(w, u);
        if (hp <= 0 || hp >= maxHp) continue;
        const int gain = seconds * rate;
        Field<uint16_t>(u, kOffHp) = static_cast<uint16_t>(hp + gain > maxHp ? maxHp : hp + gain);
    }
}

// [gold_mines] / [oil_platforms] unlimited: no code patch, the "left" word (+0x82, in hundreds) is simply put back.
// Oil platforms share the field and the decrement with gold mines (a tanker entering a platform runs the same ENTER
// action as a worker entering a mine, 0x4C99C8); the platform takes the amount over from its oil patch when it is
// created (0x4EDCB4) and hands what is left back to a new patch when it dies (0x4EE4E3).
// Each source is restored to the most it has held while we watched, so the status panel does not jump around.
// The floor of 50 (5000) keeps the computer's expansion test (> 0x31 at 0x4DBE4A) satisfied and makes sure several
// workers entering between two passes can never take a source to 0, which would destroy it.
struct SourcePeak {
    Unit* source;
    uint32_t serial;  // creation serial: a new game can reuse the same slot and tile
    int16_t x, y;
    uint16_t peak;
    bool oil;
};
constexpr int kMaxSources = 128;
constexpr uint16_t kSourceFloor = 50;
SourcePeak g_sources[kMaxSources];
int g_sourceCount = 0;

void RefillSources(const World& w) {
    const bool gold = config::g.goldMinesUnlimited, oil = config::g.oilPlatformsUnlimited;
    // Forget the peaks of a kind that is switched off: switching it back on later must not resurrect old amounts.
    int kept = 0;
    for (int k = 0; k < g_sourceCount; ++k)
        if (g_sources[k].oil ? oil : gold) g_sources[kept++] = g_sources[k];
    g_sourceCount = kept;
    if (!gold && !oil) return;

    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if ((Field<uint8_t>(u, kOffStateFlags) & 0x07) != 0) continue;
        const uint8_t type = TypeOf(u);
        const bool isOil = type != kTypeGoldMine && (w.typeFlags[type] & kTfOilPlatform) != 0;
        if (isOil ? !oil : (type != kTypeGoldMine || !gold)) continue;
        uint16_t& left = Field<uint16_t>(u, kOffResources);
        const int16_t x = Field<int16_t>(u, kOffX), y = Field<int16_t>(u, kOffY);
        const uint32_t serial = Field<uint32_t>(u, kOffSerial);
        SourcePeak* entry = nullptr;
        for (int k = 0; k < g_sourceCount; ++k)
            if (g_sources[k].source == u && g_sources[k].serial == serial && g_sources[k].x == x && g_sources[k].y == y) entry = &g_sources[k];
        if (!entry) {
            if (g_sourceCount == kMaxSources) g_sourceCount = 0;  // a new map reuses slots: start the table over
            entry = &g_sources[g_sourceCount++];
            *entry = {u, serial, x, y, left, isOil};
        }
        if (left > entry->peak) entry->peak = left;
        if (entry->peak < kSourceFloor) entry->peak = kSourceFloor;
        if (left < entry->peak) left = entry->peak;
    }
}

// [gold_mines] amount / [oil_platforms] amount: the "left" word of every mine, oil patch and platform is multiplied
// ONCE per new map. The new-map hook runs before any unit exists, so it only raises a flag; the first tick of that
// map does the work. A savegame stores the word, so a loaded game is never touched: the hook does not fire for one,
// and the game's own "came from a savegame" word is checked as well in case a save is loaded before that first tick.
// The word holds up to 65535 (6,553,500); the map format stops at 6375 (637,500), so x10 always fits.
bool g_newMapPending = false;

void ScaleNewMapSources(const World& w) {
    if (!g_newMapPending) return;
    g_newMapPending = false;
    if (*At<uint16_t>(kRvaGameFromSave) != 0) return;
    const double gold = config::g.goldMinesAmount, oil = config::g.oilAmount;
    if (gold == 1.0 && oil == 1.0) return;
    int mines = 0, wells = 0;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if ((Field<uint8_t>(u, kOffStateFlags) & 0x07) != 0) continue;
        const uint8_t type = TypeOf(u);
        const bool isMine = type == kTypeGoldMine;
        const bool isOil = !isMine && (type == kTypeOilPatch || (w.typeFlags[type] & kTfOilPlatform) != 0);
        if (!isMine && !isOil) continue;
        uint16_t& left = Field<uint16_t>(u, kOffResources);
        if (left == 0) continue;
        double scaled = left * (isMine ? gold : oil) + 0.5;
        if (scaled < 1.0) scaled = 1.0;
        if (scaled > 65535.0) scaled = 65535.0;
        left = static_cast<uint16_t>(scaled);
        ++(isMine ? mines : wells);
    }
    logx::Write("new map: gold x%.2f in %d mines, oil x%.2f in %d patches / platforms", gold, mines, oil, wells);
}

// [food] hall_food: the game keeps food supply as a running counter (farm +4, any hall tier +1, see game.h). Rather
// than patch those constants, the supply word is recomputed from the game's own per-type building counters:
//     supply = 4 x farms + amount x (halls + keeps + castles)
// It is stateless, so it survives savegames (the game re-counts after a load, the next tick re-applies), a hall being
// upgraded (the unit just moves between the three counters) and config reloads. With amount = 1 the formula IS the
// game's own value, which is how switching the option off restores it. While the option has never been on in this
// session nothing is written at all. Every player gets it, the computer included: its farm logic reads the same word.
bool g_foodTouched = false;

void SyncHallFood() {
    const bool on = config::g.hallFood;
    if (!on && !g_foodTouched) return;
    const int perHall = on ? config::g.hallFoodAmount : 1;
    uint16_t* supply = At<uint16_t>(kRvaFoodSupply);
    const uint16_t* farms = At<uint16_t>(kRvaFarmCount);
    const uint16_t* halls = At<uint16_t>(kRvaHallCount);
    const uint16_t* keeps = At<uint16_t>(kRvaKeepCount);
    const uint16_t* castles = At<uint16_t>(kRvaCastleCount);
    for (int player = 0; player < 16; ++player) {
        // A counter that went below zero wraps to ~65535; the game asserts on that path. Leave such a player alone.
        if (farms[player] > 1600 || halls[player] > 1600 || keeps[player] > 1600 || castles[player] > 1600) continue;
        unsigned want = 4u * farms[player] + static_cast<unsigned>(perHall) * (halls[player] + keeps[player] + castles[player]);
        if (want > 0xFFFF) want = 0xFFFF;
        if (supply[player] != want) supply[player] = static_cast<uint16_t>(want);
    }
    g_foodTouched = on;
}

}  // namespace

void OnTick(const World& w, unsigned elapsedMs) {
    Regenerate(w, elapsedMs);
    SyncHallFood();
    ScaleNewMapSources(w);  // before the refill, so "unlimited" remembers the scaled amount as the peak
    RefillSources(w);
}

void OnNewMap() { g_newMapPending = true; }

}  // namespace tweaks
