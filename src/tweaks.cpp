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

}  // namespace

void OnTick(const World& w, unsigned elapsedMs) {
    HeroRegen(w, elapsedMs);
    RefillSources(w);
}

}  // namespace tweaks
