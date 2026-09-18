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

}  // namespace

void OnTick(const World& w, unsigned elapsedMs) { HeroRegen(w, elapsedMs); }

}  // namespace tweaks
