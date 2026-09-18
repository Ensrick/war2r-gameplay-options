#include "tweaks.h"

#include "config.h"

using namespace game;

namespace tweaks {

namespace {

unsigned g_regenAccumMs = 0;

// [heroes] regen_hp_per_second: paced by real time that the simulation was actually stepping.
void HeroRegen(const World& w, unsigned elapsedMs) {
    const int perSecond = config::g.heroRegenPerSecond;
    if (perSecond <= 0) {
        g_regenAccumMs = 0;
        return;
    }
    g_regenAccumMs += elapsedMs;
    if (g_regenAccumMs < 1000) return;
    const int gain = static_cast<int>(g_regenAccumMs / 1000) * perSecond;
    g_regenAccumMs %= 1000;

    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (!IsActive(u) || !config::g.isHero[TypeOf(u)]) continue;
        if (config::g.heroRegenMineOnly && OwnerOf(u) != w.localPlayer) continue;
        const int hp = Field<uint16_t>(u, kOffHp), maxHp = MaxHp(w, u);
        if (hp <= 0 || hp >= maxHp) continue;
        Field<uint16_t>(u, kOffHp) = static_cast<uint16_t>(hp + gain > maxHp ? maxHp : hp + gain);
    }
}

// [gold_mines] unlimited: no code patch, the mine's "gold left" word (+0x82, in hundreds) is simply put back.
// Each mine is restored to the most it has held while we watched, so the status panel does not jump around.
// The floor of 50 (5000 gold) keeps the computer's expansion test (> 0x31 at 0x4DBE4A) satisfied too.
struct MinePeak {
    Unit* mine;
    uint32_t serial;  // creation serial: a new game can reuse the same slot and tile
    int16_t x, y;
    uint16_t peak;
};
constexpr int kMaxMines = 128;
constexpr uint16_t kMineFloor = 50;
MinePeak g_mines[kMaxMines];
int g_mineCount = 0;

void RefillGoldMines(const World& w) {
    if (!config::g.goldMinesUnlimited) {
        g_mineCount = 0;  // forget the peaks: switching it back on later must not resurrect old amounts
        return;
    }
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (TypeOf(u) != kTypeGoldMine || (Field<uint8_t>(u, kOffStateFlags) & 0x07) != 0) continue;
        uint16_t& left = Field<uint16_t>(u, kOffResources);
        const int16_t x = Field<int16_t>(u, kOffX), y = Field<int16_t>(u, kOffY);
        const uint32_t serial = Field<uint32_t>(u, kOffSerial);
        MinePeak* entry = nullptr;
        for (int k = 0; k < g_mineCount; ++k)
            if (g_mines[k].mine == u && g_mines[k].serial == serial && g_mines[k].x == x && g_mines[k].y == y) entry = &g_mines[k];
        if (!entry) {
            if (g_mineCount == kMaxMines) g_mineCount = 0;  // a new map reuses slots: start the table over
            entry = &g_mines[g_mineCount++];
            *entry = {u, serial, x, y, left};
        }
        if (left > entry->peak) entry->peak = left;
        if (entry->peak < kMineFloor) entry->peak = kMineFloor;
        if (left < entry->peak) left = entry->peak;
    }
}

}  // namespace

void OnTick(const World& w, unsigned elapsedMs) {
    HeroRegen(w, elapsedMs);
    RefillGoldMines(w);
}

}  // namespace tweaks
