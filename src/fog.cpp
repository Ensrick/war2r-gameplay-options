#include "fog.h"

#include "config.h"
#include "log.h"
#include "world.h"

using namespace game;

namespace fog {

namespace {

// A game starts (new map or savegame) when the new-map hook says so, or when the game's step counter does anything but
// stay or count up by one: the game loop sets it to 0 and a savegame restores its own. The hook alone is not enough:
// it does not fire for a savegame, and a custom game sets the flag from its game options after it (FUN_004d6800 from
// 0x4C45A0), so the flag is only written on the first tick.
bool g_newMapPending = false;
bool g_stepSeen = false;
uint32_t g_lastStep = 0;
FogOfWar g_lastWanted = FogOfWar::Game;
unsigned g_applyCount = 0;

void Apply(FogOfWar want) {
    if (want == FogOfWar::Game) return;
    uint8_t& flag = *At<uint8_t>(kRvaFogOfWar);
    const uint8_t value = want == FogOfWar::On ? 1 : 0;
    if (flag == value) return;
    flag = value;
    ++g_applyCount;
    // Off: what the Options screen does on the same change (0x4DF23E), so ground already explored is shown at once
    // instead of staying dark until a unit sees it again. On: the game's own re-fog pass takes over within 100 steps.
    const bool mapsReady = *At<uint8_t*>(kRvaVisibleMap) && *At<uint8_t*>(kRvaExploredMap) && *At<uint8_t*>(kRvaFogMaskMap);
    if (!value && mapsReady) reinterpret_cast<void(__cdecl*)()>(g_base + kRvaRevealExplored)();
    logx::Write("fog of war %s ([general] fog_of_war)", value ? "on" : "off");
}

}  // namespace

void OnNewMap() { g_newMapPending = true; }

void OnTick() {
    const uint32_t step = *At<uint32_t>(kRvaGameStep);
    const bool gameStarted = g_newMapPending || !g_stepSeen || (step != g_lastStep && step != g_lastStep + 1);
    g_newMapPending = false;
    g_stepSeen = true;
    g_lastStep = step;
    // A changed setting (config reload) is applied once too; otherwise the choice made in the Options screen stands.
    const FogOfWar want = config::g.fogOfWar;
    const bool changed = want != g_lastWanted;
    g_lastWanted = want;
    if (gameStarted || changed) Apply(want);
}

unsigned ApplyCount() { return g_applyCount; }

}  // namespace fog
