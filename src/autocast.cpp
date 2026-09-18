#include "autocast.h"

#include <windows.h>
#include <cstdlib>

#include "config.h"
#include "game.h"
#include "log.h"

using namespace game;

namespace autocast {

namespace {

using OrderHandlerFn = void(__cdecl*)(Unit*);
using IssueOrderFn = void(__cdecl*)(Unit*, int16_t, int16_t, Unit*, OrderHandlerFn);
using ShowMessageFn = void(__cdecl*)(const char*, int, int, int);

uintptr_t g_base = 0;
wchar_t g_dllDir[MAX_PATH];
bool g_initialised = false;
unsigned g_tick = 0;
bool g_toggleKeyWasDown = false;
bool g_netGameLogged = false;
unsigned g_castCount = 0;

template <typename T>
T* At(uint32_t rva) { return reinterpret_cast<T*>(g_base + rva); }

struct SpellDef {
    uint8_t order;
    uint32_t researchBit;
    bool friendly;
};

// Order ids and research bits as used by the game's own caster AI (FUN_004cb200 / 004cb2f0 / 004cac80 / 004cb480).
const SpellDef kSpells[kSpellCount] = {
    {0x27, 0x00002, true},   // heal
    {0x29, 0x00008, false},  // exorcism
    {0x2C, 0x00040, false},  // slow
    {0x2E, 0x00100, false},  // polymorph
    {0x31, 0x00800, true},   // bloodlust
    {0x33, 0x04000, false},  // death coil
    {0x35, 0x10000, true},   // haste
    {0x36, 0x20000, true},   // unholy armor
};

struct Claim {
    uint8_t order;
    Unit* target;
};
constexpr int kMaxClaims = 64;
Claim g_claims[kMaxClaims];
int g_claimCount = 0;

struct World {
    Unit* units;
    unsigned unitCount;
    Unit** grid;
    int mapSize;
    uint8_t localPlayer;
    const uint8_t* alliance;
    const uint32_t* typeFlags;
    const uint16_t* maxHpByType;
};

bool IsActive(Unit* u) { return (Field<uint8_t>(u, kOffStateFlags) & 0x0F) == 0; }

bool Allied(const World& w, uint8_t a, uint8_t b) { return w.alliance[a * kMaxPlayers + b] != 0; }

bool IsEnemy(const World& w, uint8_t me, Unit* u) {
    const uint8_t owner = Field<uint8_t>(u, kOffOwner);
    return owner < kNeutralPlayer && !Allied(w, me, owner);
}

int MaxHp(const World& w, Unit* u) {
    const int hp = w.maxHpByType[Field<uint8_t>(u, kOffType)];
    return hp ? hp : 1;
}

int Distance(Unit* a, Unit* b) {
    const int dx = abs(Field<int16_t>(a, kOffX) - Field<int16_t>(b, kOffX));
    const int dy = abs(Field<int16_t>(a, kOffY) - Field<int16_t>(b, kOffY));
    return dx > dy ? dx : dy;
}

// Calls fn(unit) for every distinct-tile unit within `radius` tiles of `centre`; stops early when fn returns true.
template <typename Fn>
bool ScanGrid(const World& w, Unit* centre, int radius, Fn fn) {
    const int cx = Field<int16_t>(centre, kOffX), cy = Field<int16_t>(centre, kOffY);
    for (int y = cy - radius; y <= cy + radius; ++y) {
        if (y < 0 || y >= w.mapSize) continue;
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (x < 0 || x >= w.mapSize) continue;
            Unit* u = w.grid[y * w.mapSize + x];
            if (u && u != centre && IsActive(u) && fn(u)) return true;
        }
    }
    return false;
}

bool EnemyNear(const World& w, Unit* unit, uint8_t me, int radius) {
    return ScanGrid(w, unit, radius, [&](Unit* u) {
        return IsEnemy(w, me, u) && Field<uint16_t>(u, kOffInvisTimer) == 0;
    });
}

bool IsFighting(const World& w, Unit* u, uint8_t me) {
    switch (Field<uint8_t>(u, kOffOrder)) {
    case kOrderDefend:  // the game's own bloodlust / haste criterion
        return true;
    case kOrderAttack:
    case kOrderAttackTarget:
    case kOrderAttackArea:
    case kOrderAttackWall:
    case kOrderStandAttack:
    case kOrderDefendStopped:
        return EnemyNear(w, u, me, config::g.combatRadius);
    default:
        return false;
    }
}

bool IsClaimed(uint8_t order, Unit* target) {
    for (int i = 0; i < g_claimCount; ++i)
        if (g_claims[i].order == order && g_claims[i].target == target) return true;
    return false;
}

// Higher score wins, negative = not eligible.
int ScoreTarget(const World& w, Spell spell, Unit* caster, Unit* t) {
    const uint8_t me = Field<uint8_t>(caster, kOffOwner);
    const uint8_t owner = Field<uint8_t>(t, kOffOwner);
    const uint32_t tf = w.typeFlags[Field<uint8_t>(t, kOffType)];
    const int closeness = 64 - Distance(caster, t);

    if (kSpells[spell].friendly) {
        if (config::g.ownUnitsOnly ? owner != me : !Allied(w, me, owner)) return -1;
    } else {
        if (!IsEnemy(w, me, t) || Field<uint16_t>(t, kOffInvisTimer) != 0) return -1;
    }

    switch (spell) {
    case kSpellHeal: {
        if (!(tf & kTfFleshy)) return -1;
        const int hp = Field<uint16_t>(t, kOffHp), maxHp = MaxHp(w, t);
        if (hp >= maxHp || hp * 100 > maxHp * config::g.healBelowPct) return -1;
        return 1000 - hp * 1000 / maxHp;  // most hurt first
    }
    case kSpellExorcism:
        return (tf & kTfUndead) ? closeness : -1;
    case kSpellSlow:
        if ((tf & kTfBuilding) || !(tf & (kTfAttacker | kTfCaster))) return -1;
        if (Field<int16_t>(t, kOffHasteTimer) < 0 || Field<uint8_t>(t, kOffOrder) == kOrderStop) return -1;
        return closeness;
    case kSpellPolymorph:
        if (!(tf & kTfFleshy)) return -1;
        // Dragons, gryphons, daemons, Deathwing. Always eligible: a daemon's 60 max HP would fail polymorph_min_hp.
        if ((tf & kTfFlyer) && (tf & kTfAttacker)) return 200000 + MaxHp(w, t) * 100 + closeness;
        if (tf & kTfCaster) return 100000 + closeness;
        if (!(tf & kTfAttacker) || MaxHp(w, t) < config::g.polymorphMinHp) return -1;
        return MaxHp(w, t) * 100 + closeness;  // biggest unit first
    case kSpellDeathCoil:
        return (tf & kTfFleshy) ? closeness : -1;
    case kSpellBloodlust:
        if (!(tf & kTfFleshy) || Field<uint16_t>(t, kOffBloodTimer) != 0) return -1;
        return IsFighting(w, t, me) ? closeness : -1;
    case kSpellHaste:
        if ((tf & kTfBuilding) || Field<int16_t>(t, kOffHasteTimer) != 0) return -1;
        return IsFighting(w, t, me) ? closeness : -1;
    case kSpellUnholyArmor:
        if ((tf & kTfBuilding) || Field<uint16_t>(t, kOffArmorTimer) != 0) return -1;
        if (Field<uint16_t>(t, kOffHp) > MaxHp(w, t) / 2) return -1;  // the game AI's own threshold
        return IsFighting(w, t, me) ? closeness : -1;
    default:
        return -1;
    }
}

bool TryCast(const World& w, Unit* caster, Spell spell) {
    if (!config::g.spell[spell]) return false;
    const SpellDef& def = kSpells[spell];
    const uint8_t me = Field<uint8_t>(caster, kOffOwner);
    if (!(At<uint32_t>(kRvaSpellsResearched)[me] & def.researchBit)) return false;
    if (Field<uint8_t>(caster, kOffMana) < At<uint16_t>(kRvaManaCostByOrder)[def.order]) return false;

    Unit* best = nullptr;
    int bestScore = -1;
    ScanGrid(w, caster, config::g.searchRadius, [&](Unit* t) {
        if (IsClaimed(def.order, t)) return false;
        const int score = ScoreTarget(w, spell, caster, t);
        if (score > bestScore) {
            bestScore = score;
            best = t;
        }
        return false;
    });
    if (!best) return false;

    // Same sequence the game's AI cast helper (FUN_004cb0e0) uses.
    *At<uint16_t>(kRvaPendingSpellOrder) = def.order;
    reinterpret_cast<IssueOrderFn>(g_base + kRvaIssueOrder)(
        caster, 0, 0, best, reinterpret_cast<OrderHandlerFn>(g_base + kRvaSpellOrderHandler));
    *At<uint16_t>(kRvaPendingSpellOrder) = 0;

    if (Field<uint8_t>(caster, kOffOrder) != def.order) return false;  // order was not interruptible
    if (g_claimCount < kMaxClaims) g_claims[g_claimCount++] = {def.order, best};
    ++g_castCount;
    if (config::g.logCasts)
        logx::Write("cast %s: caster type %u at %d,%d -> target type %u owner %u at %d,%d", config::kSpellKeys[spell],
                    Field<uint8_t>(caster, kOffType), Field<int16_t>(caster, kOffX), Field<int16_t>(caster, kOffY),
                    Field<uint8_t>(best, kOffType), Field<uint8_t>(best, kOffOwner), Field<int16_t>(best, kOffX),
                    Field<int16_t>(best, kOffY));
    return true;
}

bool OrderAllowsAutocast(uint8_t order) {
    switch (order) {
    case kOrderStop:
    case kOrderDefend:
    case kOrderStand:
    case kOrderStandAttack:
    case kOrderDefendGround:
    case kOrderDefendStopped:
        return true;
    case kOrderAttack:
    case kOrderAttackTarget:
    case kOrderAttackArea:
    case kOrderAttackWall:
    case kOrderMovePatrol:
    case kOrderPatrol:
        return config::g.castWhileAttacking;
    default:
        return false;  // moving, following, boarding, already casting...
    }
}

void CasterThink(const World& w, Unit* caster) {
    switch (Field<uint8_t>(caster, kOffType)) {
    case kTypePaladin:
    case kTypePaladinHero:
        TryCast(w, caster, kSpellHeal) || TryCast(w, caster, kSpellExorcism);
        break;
    case kTypeOgreMage:
    case kTypeOgreMageHero:
        TryCast(w, caster, kSpellBloodlust);
        break;
    case kTypeMage:
    case kTypeMageHero:
        TryCast(w, caster, kSpellPolymorph) || TryCast(w, caster, kSpellSlow);
        break;
    case kTypeDeathKnight:
    case kTypeDeathKnightHero:
        TryCast(w, caster, kSpellUnholyArmor) || TryCast(w, caster, kSpellDeathCoil) || TryCast(w, caster, kSpellHaste);
        break;
    }
}

void ShowMessage(const char* text) {
    reinterpret_cast<ShowMessageFn>(g_base + kRvaShowMessage)(text, 8, 100, 0);
}

bool GameWindowFocused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void PollToggleKey() {
    const int vk = config::g.toggleKey;
    if (!vk) return;
    const bool down = (GetAsyncKeyState(vk) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && GameWindowFocused();
    if (down && !g_toggleKeyWasDown) {
        config::g.enabled = !config::g.enabled;
        logx::Write("toggle key: autocast %s", config::g.enabled ? "on" : "off");
        ShowMessage(config::g.enabled ? "Autocast ON" : "Autocast OFF");
    }
    g_toggleKeyWasDown = down;
}

void Pass() {
    World w;
    w.units = *At<Unit*>(kRvaUnitArray);
    w.unitCount = *At<uint32_t>(kRvaUnitCount) & 0xFFFF;
    w.grid = *At<Unit**>(kRvaUnitGrid);
    w.mapSize = *At<uint16_t>(kRvaMapSize);
    w.localPlayer = *At<uint8_t>(kRvaLocalPlayer);
    w.alliance = At<uint8_t>(kRvaAlliance);
    w.typeFlags = At<uint32_t>(kRvaTypeFlags);
    w.maxHpByType = At<uint16_t>(kRvaMaxHpByType);
    if (!w.units || !w.grid || w.mapSize <= 0 || w.mapSize > 256 || w.unitCount == 0) return;
    if (w.localPlayer >= kMaxPlayers || At<uint8_t>(kRvaController)[w.localPlayer] != 0) return;  // 0 = human

    auto unitAt = [&](unsigned i) {
        return reinterpret_cast<Unit*>(reinterpret_cast<uint8_t*>(w.units) + i * kUnitSize);
    };

    // Casts already under way, so two casters never pick the same target for the same spell.
    g_claimCount = 0;
    for (unsigned i = 0; i < w.unitCount && g_claimCount < kMaxClaims; ++i) {
        Unit* u = unitAt(i);
        const uint8_t order = Field<uint8_t>(u, kOffOrder);
        if (Field<uint8_t>(u, kOffOwner) != w.localPlayer || !IsActive(u) || order < kOrderSpellFirst) continue;
        if (Unit* target = Field<Unit*>(u, kOffOrderTarget)) g_claims[g_claimCount++] = {order, target};
    }

    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = unitAt(i);
        if (Field<uint8_t>(u, kOffOwner) != w.localPlayer || !IsActive(u)) continue;
        if (!(w.typeFlags[Field<uint8_t>(u, kOffType)] & kTfCaster)) continue;
        if (Field<uint16_t>(u, kOffInvisTimer) != 0) continue;  // casting would break the player's invisibility
        if (!OrderAllowsAutocast(Field<uint8_t>(u, kOffOrder))) continue;
        CasterThink(w, u);
    }
}

}  // namespace

void SetModuleBase(uintptr_t exeBase, const wchar_t* dllDir) {
    g_base = exeBase;
    wcscpy_s(g_dllDir, dllDir);
}

void __cdecl OnTick() {
    if (!g_initialised) {
        g_initialised = true;
        config::Init(g_dllDir);
        logx::Write("first game tick, autocast live");
    }
    ++g_tick;
    if (g_tick % 64 == 0) config::ReloadIfChanged();
    PollToggleKey();
    if (!config::g.enabled || g_tick % static_cast<unsigned>(config::g.intervalTicks) != 0) return;

    // Orders issued here bypass the network command queue, which would desync a multiplayer game.
    if (*At<uint32_t>(kRvaNetGame) != 0) {
        if (!g_netGameLogged) {
            g_netGameLogged = true;
            logx::Write("multiplayer game detected, autocast stays off");
        }
        return;
    }
    g_netGameLogged = false;
    Pass();
}

void RunPass() { Pass(); }

}  // namespace autocast
