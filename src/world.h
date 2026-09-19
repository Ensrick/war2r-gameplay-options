// Snapshot of the game's globals for one pass, plus the small unit helpers every feature shares.
#pragma once
#include <cstdint>
#include <cstdlib>

#include "game.h"

namespace game {

extern uintptr_t g_base;  // load address of Warcraft II.exe (ASLR), set once at attach

template <typename T>
inline T* At(uint32_t rva) { return reinterpret_cast<T*>(g_base + rva); }

struct World {
    Unit* units;
    unsigned unitCount;
    Unit** grid;     // land / sea layer
    Unit** airGrid;  // flyers live ONLY here, never in `grid`
    int mapSize;
    uint8_t localPlayer;
    const uint8_t* alliance;
    const uint32_t* typeFlags;
    uint16_t* maxHpByType;
};

// False when no game is running or the local player is not a human (attract mode, observers).
bool BuildWorld(World& w);

inline Unit* UnitAt(const World& w, unsigned i) {
    return reinterpret_cast<Unit*>(reinterpret_cast<uint8_t*>(w.units) + i * kUnitSize);
}

// Current-or-pending order, see EffectiveOrder.
inline uint8_t OrderOf(Unit* u) { return EffectiveOrder(reinterpret_cast<const uint8_t*>(u)); }
inline bool IsActive(Unit* u) { return (Field<uint8_t>(u, kOffStateFlags) & 0x0F) == 0; }
inline uint8_t TypeOf(Unit* u) { return Field<uint8_t>(u, kOffType); }
inline uint8_t OwnerOf(Unit* u) { return Field<uint8_t>(u, kOffOwner); }
inline bool Allied(const World& w, uint8_t a, uint8_t b) { return w.alliance[a * kMaxPlayers + b] != 0; }

inline bool IsEnemy(const World& w, uint8_t me, Unit* u) {
    const uint8_t owner = OwnerOf(u);
    return owner < kNeutralPlayer && !Allied(w, me, owner);
}

inline int MaxHp(const World& w, Unit* u) {
    const int hp = w.maxHpByType[TypeOf(u)];
    return hp ? hp : 1;
}

inline int Distance(Unit* a, Unit* b) {
    const int dx = abs(Field<int16_t>(a, kOffX) - Field<int16_t>(b, kOffX));
    const int dy = abs(Field<int16_t>(a, kOffY) - Field<int16_t>(b, kOffY));
    return dx > dy ? dx : dy;
}

// Calls fn(unit) for every grid entry (both layers: ground, then air) within `radius` tiles of `centre`, corpses
// included; stops when fn returns true.
template <typename Fn>
inline bool ScanGridRaw(const World& w, Unit* centre, int radius, Fn fn) {
    const int cx = Field<int16_t>(centre, kOffX), cy = Field<int16_t>(centre, kOffY);
    for (int y = cy - radius; y <= cy + radius; ++y) {
        if (y < 0 || y >= w.mapSize) continue;
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (x < 0 || x >= w.mapSize) continue;
            const int tile = y * w.mapSize + x;
            Unit* u = w.grid[tile];
            if (u && u != centre && fn(u)) return true;
            u = w.airGrid ? w.airGrid[tile] : nullptr;  // a flyer can share the tile with a ground unit
            if (u && u != centre && fn(u)) return true;
        }
    }
    return false;
}

// Same, live units only.
template <typename Fn>
inline bool ScanGrid(const World& w, Unit* centre, int radius, Fn fn) {
    return ScanGridRaw(w, centre, radius, [&](Unit* u) { return IsActive(u) && fn(u); });
}

// The game's own IssueOrder (FUN_004ef210). handlerRva is an entry of the order handler table (0x8C1498).
void IssueOrder(Unit* unit, int16_t x, int16_t y, Unit* target, uint32_t handlerRva);
// Spell cast the way the AI helpers and the player command path both do it: pending spell id around IssueOrder.
void IssueSpell(Unit* caster, uint8_t order, int16_t x, int16_t y, Unit* target);

void ShowMessage(const char* text);  // the game's own banner line (cheat-toggle style)

}  // namespace game
