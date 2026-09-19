#include "autocast.h"

#include <windows.h>
#include <cstdlib>

#include "config.h"
#include "log.h"
#include "world.h"

using namespace game;

namespace autocast {

namespace {

unsigned g_castCount = 0;

struct SpellDef {
    uint8_t order;
    uint32_t researchBit;
    bool friendly;
};

// Order ids and research bits as used by the game's own caster AI (FUN_004cb200 / 004cb2f0 / 004cac80 / 004cb480); the
// second group also matches the spell button records at 0x8C7488.. (byte +16 = research bit, byte +17 = order).
const SpellDef kSpells[kSpellCount] = {
    {0x27, 0x00002, true},   // heal
    {0x29, 0x00008, false},  // exorcism
    {0x2C, 0x00040, false},  // slow
    {0x2E, 0x00100, false},  // polymorph
    {0x31, 0x00800, true},   // bloodlust
    {0x33, 0x04000, false},  // death coil
    {0x35, 0x10000, true},   // haste
    {0x36, 0x20000, true},   // unholy armor
    {0x32, 0x02000, false},  // raise dead (cast at a tile, not at a unit)
    {kOrderHolyVision, 0x00001, false},     // a tile, range 0xFF
    {kOrderFlameShield, 0x00010, true},     // an own melee unit
    {kOrderFireball, 0x00020, false},       // a tile
    {kOrderInvisibility, 0x00080, true},    // an own unit
    {kOrderBlizzard, 0x00200, false},       // a tile, channelled
    {kOrderDeathAndDecay, 0x80000, false},  // a tile, channelled
    {kOrderWhirlwind, 0x08000, false},      // a tile
    {kOrderRunes, 0x40000, false},          // a tile
};

// Friendly fire. Every damaging spell below splashes through FUN_004afb50, which has no owner test: it hurts every unit,
// flyer and building whose centre is within 42 px of an impact, own and allied ones included, and spares only the
// missile's source unit. Runes have no owner at all. The distances come from docs/research/autocast_all_spells.md.
constexpr int kChannelClearance = 4;      // Blizzard / Death and Decay: impacts on the 5x5 tiles around the target, 42 px
                                          // of splash, +1 for a unit that is between two tiles
constexpr int kChannelWalls = 3;          // a wall is hit on an impact tile and on its 4 neighbours (FUN_004bd850)
constexpr int kChannelReach = 3;          // where a wave can still hurt anybody
constexpr int kWhirlwindClearance = 6;    // it wanders at random for 800 updates (FUN_004aeb70)
constexpr int kFireballClearance = 2;     // around every point of the splash line
constexpr int kFireballWalls = 1;
constexpr int kFlameShieldClearance = 3;  // the ring is up to 34 px from the unit, plus 42 px of splash: 2.4 tiles
constexpr int kFlameShieldMinEnemies = 2;
constexpr int kRunesClearance = 6;
constexpr int kRunesMinEnemies = 2;
constexpr int kRuneSpacing = 2;           // no new runes this close to a live one
constexpr int kAreaCount = 2;             // enemies are counted this close to a Blizzard / Death and Decay / Whirlwind tile
constexpr int kHolyVisionHalfWidth = 15, kHolyVisionHalfHeight = 17;  // the 7 sight-9 windows of FUN_004e2720

struct Claim {
    uint8_t order;
    Unit* target;  // null for a positional cast, then x/y identify it
    int16_t x, y;
};
constexpr int kMaxClaims = 64;
Claim g_claims[kMaxClaims];
int g_claimCount = 0;

// Blizzard and Death and Decay never end by themselves (FUN_004e19a0 / FUN_004e2530 never set stop): a new wave every
// cast cycle until the mana is gone. The channels the mod started are kept here and stopped once they turn unsafe or
// useless. Keyed by unit pointer + creation serial; a channel the player ordered is never in the list.
struct Channel {
    Unit* caster;
    uint32_t serial;
    uint8_t order;
    int16_t x, y;
};
constexpr int kMaxChannels = 32;
Channel g_channels[kMaxChannels];
int g_channelCount = 0;

// Own and allied buildings with their whole footprint (x, y is the top-left tile, FUN_004b4910). The game files a
// building on every tile of its footprint in the unit grid; this list does not depend on that.
struct Footprint {
    Unit* unit;
    int x0, y0, x1, y1;
};
constexpr int kMaxFootprints = 2048;
Footprint g_footprints[kMaxFootprints];
int g_footprintCount = 0;

struct Size {
    uint16_t w, h;
};

int g_unexploredSum[129 * 129];  // prefix sums of never-explored tiles, built at most once per pass
bool g_sumsTried = false, g_sumsReady = false;

int X(Unit* u) { return Field<int16_t>(u, kOffX); }
int Y(Unit* u) { return Field<int16_t>(u, kOffY); }
bool OnMap(const World& w, int x, int y) { return x >= 0 && y >= 0 && x < w.mapSize && y < w.mapSize; }
int ManaCost(uint8_t order) { return At<uint16_t>(kRvaManaCostByOrder)[order]; }

// Tiles a caster looks for a target: search_radius, but never beyond the spell's own range, so it does not walk off.
int Reach(uint8_t order) {
    const int range = At<uint8_t>(kRvaOrderRange)[order];
    return range < config::g.searchRadius ? range : config::g.searchRadius;
}

bool CanBeHurt(Unit* u) { return (Field<uint8_t>(u, kOffStateFlags) & 0x07) == 0; }  // FUN_004bd8f0's first test
bool IsFriend(const World& w, Unit* u) { return OwnerOf(u) == w.localPlayer || Allied(w, w.localPlayer, OwnerOf(u)); }

// An enemy a spell would really hurt: visible, and not under Unholy Armor (FUN_004bd8f0 skips those).
bool IsTarget(const World& w, uint8_t me, Unit* u) {
    return IsActive(u) && IsEnemy(w, me, u) && Field<uint16_t>(u, kOffInvisTimer) == 0 && Field<uint16_t>(u, kOffArmorTimer) == 0;
}

// Switch on, researched, mana for it.
bool Ready(Unit* caster, Spell spell, int manaNeeded) {
    if (!config::g.spell[spell]) return false;
    if (!(At<uint32_t>(kRvaSpellsResearched)[OwnerOf(caster)] & kSpells[spell].researchBit)) return false;
    return Field<uint8_t>(caster, kOffMana) >= manaNeeded;
}

void CollectFriendlyBuildings(const World& w) {
    g_footprintCount = 0;
    const Size* sizes = At<Size>(kRvaUnitSizeByType);
    for (unsigned i = 0; i < w.unitCount && g_footprintCount < kMaxFootprints; ++i) {
        Unit* u = UnitAt(w, i);
        if (!CanBeHurt(u) || !(w.typeFlags[TypeOf(u)] & kTfBuilding) || !IsFriend(w, u)) continue;
        const Size s = sizes[TypeOf(u)];
        g_footprints[g_footprintCount++] = {u, X(u), Y(u), X(u) + (s.w ? s.w - 1 : 0), Y(u) + (s.h ? s.h - 1 : 0)};
    }
}

// Walls take splash damage too and have no owner, so any wall counts as the player's.
bool WallNear(const World& w, int cx, int cy, int radius) {
    const uint16_t* sq = *At<uint16_t*>(kRvaSquareFlags);
    if (radius < 0 || !sq) return false;
    for (int y = cy - radius; y <= cy + radius; ++y)
        for (int x = cx - radius; x <= cx + radius; ++x)
            if (OnMap(w, x, y) && (sq[y * w.mapSize + x] & kSqWalls)) return true;
    return false;
}

// True when an own or allied unit, flyer or building (any tile of its footprint) is within `radius` tiles of x, y, or a
// wall within `walls` tiles (-1 = walls do not matter). `spared` is the one unit the engine exempts (the missile source).
bool FriendlyInDanger(const World& w, int x, int y, int radius, Unit* spared, int walls) {
    if (ScanTileRaw(w, x, y, radius, [&](Unit* u) { return u != spared && CanBeHurt(u) && IsFriend(w, u); })) return true;
    for (int i = 0; i < g_footprintCount; ++i) {
        const Footprint& b = g_footprints[i];
        if (b.unit != spared && x >= b.x0 - radius && x <= b.x1 + radius && y >= b.y0 - radius && y <= b.y1 + radius) return true;
    }
    return WallNear(w, x, y, walls);
}

// Distinct enemies within `radius` tiles that a spell would hurt; a building counts once, whatever its size.
int CountEnemies(const World& w, uint8_t me, int x, int y, int radius, bool groundUnitsOnly) {
    Unit* seen[64];
    int n = 0;
    ScanTileRaw(w, x, y, radius, [&](Unit* u) {
        if (!IsTarget(w, me, u)) return false;
        if (groundUnitsOnly && (w.typeFlags[TypeOf(u)] & (kTfFlyer | kTfBuilding))) return false;
        for (int i = 0; i < n; ++i)
            if (seen[i] == u) return false;
        if (n < 64) seen[n++] = u;
        return false;
    });
    return n;
}

bool EnemyNear(const World& w, Unit* unit, uint8_t me, int radius) {
    return ScanGrid(w, unit, radius, [&](Unit* u) {
        return IsEnemy(w, me, u) && Field<uint16_t>(u, kOffInvisTimer) == 0;
    });
}

bool IsFighting(const World& w, Unit* u, uint8_t me) {
    switch (OrderOf(u)) {
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

bool IsTileClaimed(uint8_t order, int16_t x, int16_t y) {
    for (int i = 0; i < g_claimCount; ++i)
        if (g_claims[i].order == order && !g_claims[i].target && g_claims[i].x == x && g_claims[i].y == y) return true;
    return false;
}

// A positional cast of `order` within `radius` tiles of x, y (the player's own casts included).
bool IsAreaClaimed(uint8_t order, int x, int y, int radius) {
    for (int i = 0; i < g_claimCount; ++i) {
        const Claim& c = g_claims[i];
        if (c.order == order && !c.target && abs(c.x - x) <= radius && abs(c.y - y) <= radius) return true;
    }
    return false;
}

bool AreaSpellNear(int x, int y, int radius) {
    return IsAreaClaimed(kOrderBlizzard, x, y, radius) || IsAreaClaimed(kOrderDeathAndDecay, x, y, radius) ||
           IsAreaClaimed(kOrderWhirlwind, x, y, radius);
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
        if (maxHp - hp < config::g.healMinMissingHp || hp * 100 > maxHp * config::g.healBelowPct) return -1;
        return 1000 - hp * 1000 / maxHp;  // most hurt first
    }
    case kSpellExorcism:
        return (tf & kTfUndead) ? closeness : -1;
    case kSpellSlow:
        if ((tf & kTfBuilding) || !(tf & (kTfAttacker | kTfCaster))) return -1;
        if (Field<int16_t>(t, kOffHasteTimer) < 0 || OrderOf(t) == kOrderStop) return -1;
        return closeness;
    case kSpellPolymorph: {
        // [polymorph] targets in the config is the whole rule: listed types only, earlier in the list wins.
        const uint8_t rank = config::g.polymorphRank[Field<uint8_t>(t, kOffType)];
        if (!rank || !(tf & kTfFleshy)) return -1;
        return (256 - rank) * 1000 + closeness;
    }
    case kSpellDeathCoil:
        return (tf & kTfFleshy) ? closeness : -1;
    case kSpellBloodlust:
        if (!(tf & kTfFleshy) || Field<uint16_t>(t, kOffBloodTimer) != 0) return -1;
        return IsFighting(w, t, me) ? closeness : -1;
    case kSpellHaste: {
        if ((tf & kTfBuilding) || Field<int16_t>(t, kOffHasteTimer) != 0) return -1;
        if (!config::g.hasteFlyersOnly) return IsFighting(w, t, me) ? closeness : -1;
        if (!(tf & kTfFlyer)) return -1;
        // A flyer sent to attack is hasted on the way in, not only once it is already trading blows.
        const uint8_t order = OrderOf(t);
        const bool sentToAttack = order >= kOrderAttack && order <= kOrderAttackWall;
        return (sentToAttack || IsFighting(w, t, me)) ? closeness : -1;
    }
    case kSpellUnholyArmor:
        if ((tf & kTfBuilding) || Field<uint16_t>(t, kOffArmorTimer) != 0) return -1;
        if (Field<uint16_t>(t, kOffHp) > MaxHp(w, t) / 2) return -1;  // the game AI's own threshold
        return IsFighting(w, t, me) ? closeness : -1;
    case kSpellFlameShield: {
        // The computer's own-unit rule (FUN_004ca700): a melee unit in a fight with nobody friendly around it. The
        // shield burns everyone near the shielded unit but the unit itself, the casting mage included. The hit-frame
        // action refuses flyers and shielded units (FUN_004e2110), the step action buildings (FUN_004e2900).
        if (owner != me || (tf & (kTfBuilding | kTfFlyer)) || Field<uint16_t>(t, kOffFlameTimer) != 0) return -1;
        if (At<uint8_t>(kRvaAttackRangeByType)[TypeOf(t)] != 1 || !IsFighting(w, t, me)) return -1;
        const int enemies = CountEnemies(w, me, X(t), Y(t), 2, false);
        if (enemies < kFlameShieldMinEnemies) return -1;
        if (FriendlyInDanger(w, X(t), Y(t), kFlameShieldClearance, t, kFlameShieldClearance)) return -1;
        return enemies * 100 + closeness;
    }
    case kSpellInvisibility: {
        // A hurt caster or ranged unit (the computer's target kinds, FUN_004cab20) that the player is pulling back
        // (moving) with an enemy close. The engine lets a cast reset a running invisibility for 200 mana: never here.
        if (owner != me || (tf & kTfBuilding) || Field<uint16_t>(t, kOffInvisTimer) != 0) return -1;
        if (!(tf & kTfCaster) && At<uint8_t>(kRvaAttackRangeByType)[TypeOf(t)] == 1) return -1;
        const int hp = Field<uint16_t>(t, kOffHp), maxHp = MaxHp(w, t);
        if (hp * 2 > maxHp || OrderOf(t) != kOrderMove || !EnemyNear(w, t, me, config::g.combatRadius)) return -1;
        return 1000 - hp * 1000 / maxHp;
    }
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

    IssueSpell(caster, def.order, 0, 0, best);
    if (OrderOf(caster) != def.order) return false;  // order was not interruptible
    if (g_claimCount < kMaxClaims) g_claims[g_claimCount++] = {def.order, best, 0, 0};
    ++g_castCount;
    if (config::g.logCasts)
        logx::Write("cast %s: caster type %u at %d,%d -> target type %u owner %u at %d,%d", config::kSpellKeys[spell],
                    Field<uint8_t>(caster, kOffType), Field<int16_t>(caster, kOffX), Field<int16_t>(caster, kOffY),
                    Field<uint8_t>(best, kOffType), Field<uint8_t>(best, kOffOwner), Field<int16_t>(best, kOffX),
                    Field<int16_t>(best, kOffY));
    return true;
}

// Every tile comes from a live unit or from arithmetic kept inside the map; checked once more here, so no positional
// cast ever depends on the off-map guard in game::IssueOrder.
bool CastAtTile(const World& w, Unit* caster, Spell spell, int x, int y, int enemies) {
    if (!OnMap(w, x, y)) return false;
    const uint8_t order = kSpells[spell].order;
    IssueSpell(caster, order, static_cast<int16_t>(x), static_cast<int16_t>(y), nullptr);
    if (OrderOf(caster) != order) return false;
    if (g_claimCount < kMaxClaims) g_claims[g_claimCount++] = {order, nullptr, static_cast<int16_t>(x), static_cast<int16_t>(y)};
    ++g_castCount;
    if (config::g.logCasts)
        logx::Write("cast %s: caster type %u at %d,%d -> tile %d,%d (%d)", config::kSpellKeys[spell], TypeOf(caster), X(caster),
                    Y(caster), x, y, enemies);
    return true;
}

// Raise Dead follows the computer's own rule (FUN_004cac80 -> FUN_004cb3e0 with filter FUN_004ca8d0): a corpse (type
// 0x69, state 2, not hidden) in the GROUND grid inside the 31 x 31 tiles around the death knight, whatever [autocast]
// search_radius says, and no enemy has to be near. Cast at the corpse's own tile, which is on the map. The computer marks
// the corpse (+0x4C |= 0x20); the mod claims the tile instead and writes nothing into the unit. The nearest corpse wins
// (the computer takes the first one in its scan order).
constexpr int kRaiseDeadBox = 15;  // FUN_004cb3e0: x-15 .. x+15, y-15 .. y+15

bool TryRaiseDead(const World& w, Unit* caster) {
    if (!config::g.spell[kSpellRaiseDead]) return false;
    const SpellDef& def = kSpells[kSpellRaiseDead];
    const uint8_t me = Field<uint8_t>(caster, kOffOwner);
    if (!(At<uint32_t>(kRvaSpellsResearched)[me] & def.researchBit)) return false;
    if (Field<uint8_t>(caster, kOffMana) < At<uint16_t>(kRvaManaCostByOrder)[def.order]) return false;

    Unit* best = nullptr;
    int bestDistance = 1 << 30;
    const int cx = X(caster), cy = Y(caster);
    for (int y = cy - kRaiseDeadBox; y <= cy + kRaiseDeadBox; ++y)
        for (int x = cx - kRaiseDeadBox; x <= cx + kRaiseDeadBox; ++x) {
            if (!OnMap(w, x, y)) continue;
            Unit* t = w.grid[y * w.mapSize + x];  // the ground grid only, as the computer reads it
            if (!t || t == caster || TypeOf(t) != kTypeCorpse) continue;
            const uint8_t state = Field<uint8_t>(t, kOffStateFlags);
            if ((state & 0x0F) != kStateDying || (state & 0x08)) continue;
            if (IsTileClaimed(def.order, Field<int16_t>(t, kOffX), Field<int16_t>(t, kOffY))) continue;
            const int d = Distance(caster, t);
            if (d < bestDistance) {
                bestDistance = d;
                best = t;
            }
        }
    if (!best || !OnMap(w, X(best), Y(best))) return false;

    const int16_t x = Field<int16_t>(best, kOffX), y = Field<int16_t>(best, kOffY);
    IssueSpell(caster, def.order, x, y, nullptr);
    if (OrderOf(caster) != def.order) return false;
    if (g_claimCount < kMaxClaims) g_claims[g_claimCount++] = {def.order, nullptr, x, y};
    ++g_castCount;
    if (config::g.logCasts)
        logx::Write("cast raise_dead: caster at %d,%d -> corpse at %d,%d", Field<int16_t>(caster, kOffX),
                    Field<int16_t>(caster, kOffY), x, y);
    return true;
}

int RoundDiv(int num, int den) {  // den > 0; halves round away from zero
    return num >= 0 ? (2 * num + den) / (2 * den) : -((2 * -num + den) / (2 * den));
}

// FUN_004ae7c0: the fireball drops to half speed 50 px before the aimed tile and splashes every 8 updates, 5 times: on
// the aimed tile and about 1.5, 3, 4.5 and 6 tiles past it along the caster -> target line. Points every half tile from
// half a tile before the aimed tile to 7 tiles past it (some may lie off the map). Needs caster tile != aimed tile.
template <typename Fn>
bool ForEachFireballPoint(int cx, int cy, int ax, int ay, Fn fn) {
    const int dx = ax - cx, dy = ay - cy;
    const int len = abs(dx) > abs(dy) ? abs(dx) : abs(dy);  // the missile steps along its longer axis
    for (int half = -1; half <= 14; ++half)
        if (fn(ax + RoundDiv(half * dx, 2 * len), ay + RoundDiv(half * dy, 2 * len))) return true;
    return false;
}

bool TryFireball(const World& w, Unit* caster) {
    if (!Ready(caster, kSpellFireball, ManaCost(kOrderFireball))) return false;
    const uint8_t me = OwnerOf(caster);
    const int cx = X(caster), cy = Y(caster);
    Unit* best = nullptr;
    int bestScore = 0, bestDistance = 1 << 30;
    ScanGrid(w, caster, Reach(kOrderFireball), [&](Unit* t) {
        if (!IsTarget(w, me, t)) return false;
        const int ax = X(t), ay = Y(t), d = Distance(caster, t);
        if (!OnMap(w, ax, ay) || (ax == cx && ay == cy) || IsAreaClaimed(kOrderFireball, ax, ay, 1)) return false;
        Unit* hit[64];
        int n = 0;
        ForEachFireballPoint(cx, cy, ax, ay, [&](int px, int py) {
            ScanTileRaw(w, px, py, 1, [&](Unit* u) {
                if (!IsTarget(w, me, u)) return false;
                for (int i = 0; i < n; ++i)
                    if (hit[i] == u) return false;
                if (n < 64) hit[n++] = u;
                return false;
            });
            return false;
        });
        if (n < config::g.fireballMinEnemies || n < bestScore || (n == bestScore && d >= bestDistance)) return false;
        if (ForEachFireballPoint(cx, cy, ax, ay, [&](int px, int py) {
                return FriendlyInDanger(w, px, py, kFireballClearance, caster, kFireballWalls);
            }))
            return false;
        best = t;
        bestScore = n;
        bestDistance = d;
        return false;
    });
    return best && CastAtTile(w, caster, kSpellFireball, X(best), Y(best), bestScore);
}

// A whirlwind lives 800 missile updates (FUN_004aeb70); its missile record names the caster at +0x30 (FUN_004af5c0).
// Without a readable pool nothing can be ruled out, so it counts as in flight.
bool WhirlwindInFlight(Unit* caster) {
    const uint8_t* pool = *At<uint8_t*>(kRvaMissilePool);
    const uint32_t slots = *At<uint32_t>(kRvaMissileSlots);
    if (!pool) return true;
    for (uint32_t i = 0; i < slots && i < 4096; ++i) {
        const uint8_t* m = pool + i * kMissileSize;
        if (!(m[kMisOffFlags] & 1) && m[kMisOffType] == kMissileWhirlwind && *reinterpret_cast<Unit* const*>(m + kMisOffSource) == caster)
            return true;
    }
    return false;
}

void RememberChannel(Unit* caster, uint8_t order, int x, int y) {
    const Channel c = {caster, Field<uint32_t>(caster, kOffSerial), order, static_cast<int16_t>(x), static_cast<int16_t>(y)};
    for (int i = 0; i < g_channelCount; ++i)
        if (g_channels[i].caster == caster) {
            g_channels[i] = c;
            return;
        }
    if (g_channelCount < kMaxChannels) g_channels[g_channelCount++] = c;
}

// Blizzard, Death and Decay (channelled) and Whirlwind, at the tile of an enemy in the biggest group within reach.
bool TryAreaSpell(const World& w, Unit* caster, Spell spell) {
    const uint8_t order = kSpells[spell].order;
    const bool channel = spell != kSpellWhirlwind;
    const int cost = ManaCost(order);
    int need = cost;
    if (channel) {
        // The computer only starts a channel with mana for three waves (FUN_004cb480 / FUN_004cac80).
        need = 3 * cost;
        if (config::g.channelManaReserve + cost > need) need = config::g.channelManaReserve + cost;
        if (g_channelCount >= kMaxChannels) return false;  // a channel the watchdog could not follow is never started
    }
    if (!Ready(caster, spell, need)) return false;
    if (spell == kSpellWhirlwind && WhirlwindInFlight(caster)) return false;  // one whirlwind per caster at a time
    const int clearance = channel ? kChannelClearance : kWhirlwindClearance;
    const int walls = channel ? kChannelWalls : kWhirlwindClearance;
    const uint8_t me = OwnerOf(caster);
    Unit* best = nullptr;
    int bestScore = 0, bestDistance = 1 << 30;
    ScanGrid(w, caster, Reach(order), [&](Unit* t) {
        if (!IsTarget(w, me, t)) return false;
        const int x = X(t), y = Y(t), d = Distance(caster, t);
        if (!OnMap(w, x, y)) return false;
        const int n = CountEnemies(w, me, x, y, kAreaCount, false);
        if (n < config::g.areaMinEnemies || n < bestScore || (n == bestScore && d >= bestDistance)) return false;
        if (AreaSpellNear(x, y, clearance) || FriendlyInDanger(w, x, y, clearance, caster, walls)) return false;
        best = t;
        bestScore = n;
        bestDistance = d;
        return false;
    });
    if (!best || !CastAtTile(w, caster, spell, X(best), Y(best), bestScore)) return false;
    if (channel) RememberChannel(caster, order, X(best), Y(best));
    return true;
}

bool RuneNear(int x, int y, int radius) {
    const uint16_t* timers = At<uint16_t>(kRvaRuneTimers);
    const uint8_t* rx = At<uint8_t>(kRvaRuneX);
    const uint8_t* ry = At<uint8_t>(kRvaRuneY);
    for (int i = 0; i < kMaxRunes; ++i)
        if (timers[i] && abs(rx[i] - x) <= radius && abs(ry[i] - y) <= radius) return true;
    return false;
}

// A rune hurts whoever steps on its tile first, the caster included: FUN_004e2cd0 knows no owner and spares nobody. So
// the whole plus must be far from every own and allied unit and building. It is laid around an enemy ground group: the
// enemy's own tile gets no rune (40 mana come back), the four arms wait for its next step.
bool TryRunes(const World& w, Unit* caster) {
    if (!Ready(caster, kSpellRunes, ManaCost(kOrderRunes))) return false;
    const uint8_t me = OwnerOf(caster);
    Unit* best = nullptr;
    int bestScore = 0, bestDistance = 1 << 30;
    ScanGrid(w, caster, Reach(kOrderRunes), [&](Unit* t) {
        if (!IsTarget(w, me, t) || (w.typeFlags[TypeOf(t)] & (kTfFlyer | kTfBuilding))) return false;  // flyers never trigger one
        const int x = X(t), y = Y(t), d = Distance(caster, t);
        if (!OnMap(w, x, y)) return false;
        const int n = CountEnemies(w, me, x, y, 2, true);
        if (n < kRunesMinEnemies || n < bestScore || (n == bestScore && d >= bestDistance)) return false;
        if (RuneNear(x, y, kRuneSpacing) || IsAreaClaimed(kOrderRunes, x, y, kRuneSpacing)) return false;
        if (FriendlyInDanger(w, x, y, kRunesClearance, nullptr, -1)) return false;
        best = t;
        bestScore = n;
        bestDistance = d;
        return false;
    });
    return best && CastAtTile(w, caster, kSpellRunes, X(best), Y(best), bestScore);
}

// Prefix sums of the never-explored tiles of the local player (0x10 in the explored map). That buffer is 0x4000 bytes
// (FUN_004c6110), so a map is at most 128 x 128.
bool BuildUnexploredSums(const World& w) {
    const uint8_t* explored = *At<uint8_t*>(kRvaExploredMap);
    const int n = w.mapSize;
    if (!explored || n <= 0 || n > 128) return false;
    for (int x = 0; x <= n; ++x) g_unexploredSum[x] = 0;
    for (int y = 1; y <= n; ++y) {
        int row = 0;
        g_unexploredSum[y * 129] = 0;
        for (int x = 1; x <= n; ++x) {
            row += explored[(y - 1) * n + x - 1] == kTileUnexplored;
            g_unexploredSum[y * 129 + x] = g_unexploredSum[(y - 1) * 129 + x] + row;
        }
    }
    return true;
}

int UnexploredAround(const World& w, int cx, int cy) {
    const int n = w.mapSize;
    const int x0 = cx - kHolyVisionHalfWidth < 0 ? 0 : cx - kHolyVisionHalfWidth;
    const int y0 = cy - kHolyVisionHalfHeight < 0 ? 0 : cy - kHolyVisionHalfHeight;
    const int x1 = cx + kHolyVisionHalfWidth >= n ? n - 1 : cx + kHolyVisionHalfWidth;
    const int y1 = cy + kHolyVisionHalfHeight >= n ? n - 1 : cy + kHolyVisionHalfHeight;
    const int* s = g_unexploredSum;
    return s[(y1 + 1) * 129 + x1 + 1] - s[y0 * 129 + x1 + 1] - s[(y1 + 1) * 129 + x0] + s[y0 * 129 + x0];
}

// Holy Vision: range 0xFF, so the paladin stays put; the cast ends in "stop" (FUN_004e2720), so only idle paladins at
// full mana (the computer's own rule, FUN_004cb2f0) cast it, at the tile whose window holds the most unexplored ground.
bool TryHolyVision(const World& w, Unit* caster) {
    const uint8_t order = OrderOf(caster);
    if (order != kOrderStop && order != kOrderStand) return false;
    if (Field<uint8_t>(caster, kOffMana) != 255 || !Ready(caster, kSpellHolyVision, ManaCost(kOrderHolyVision))) return false;
    if (!g_sumsTried) {
        g_sumsTried = true;
        g_sumsReady = BuildUnexploredSums(w);
    }
    if (!g_sumsReady) return false;
    const int n = w.mapSize;
    int bestX = -1, bestY = -1, bestScore = 0, bestDistance = 1 << 30;
    for (int gy = 0; gy < n + 3; gy += 4) {
        const int y = gy < n ? gy : n - 1;  // the last row / column is always a candidate
        for (int gx = 0; gx < n + 3; gx += 4) {
            const int x = gx < n ? gx : n - 1;
            const int score = UnexploredAround(w, x, y);
            const int dx = abs(x - X(caster)), dy = abs(y - Y(caster)), d = dx > dy ? dx : dy;
            if (score == 0 || score < bestScore || (score == bestScore && d >= bestDistance)) continue;
            if (IsAreaClaimed(kOrderHolyVision, x, y, kHolyVisionHalfWidth)) continue;
            bestX = x;
            bestY = y;
            bestScore = score;
            bestDistance = d;
        }
    }
    return bestX >= 0 && CastAtTile(w, caster, kSpellHolyVision, bestX, bestY, bestScore);
}

bool InUnitArray(const World& w, Unit* u) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(w.units), p = reinterpret_cast<uintptr_t>(u);
    return p >= base && p < base + static_cast<uintptr_t>(w.unitCount) * kUnitSize && (p - base) % kUnitSize == 0;
}

const char* StopReason(const World& w, const Channel& c) {
    if (FriendlyInDanger(w, c.x, c.y, kChannelClearance, c.caster, -1)) return "a friendly unit or building is in the area";
    if (CountEnemies(w, w.localPlayer, c.x, c.y, kChannelReach, false) == 0) return "no enemy left in reach";
    if (config::g.channelManaReserve > 0 && Field<uint8_t>(c.caster, kOffMana) < config::g.channelManaReserve)
        return "mana below channel_mana_reserve";
    return nullptr;
}

// Stops (stop handler, positional at the caster's own tile) every channel the mod started that turned unsafe or useless.
void GuardChannelsImpl(const World& w) {
    int kept = 0;
    for (int i = 0; i < g_channelCount; ++i) {
        const Channel c = g_channels[i];
        Unit* u = c.caster;
        // Still the unit the mod gave this channel to, still on it at the same tile? Otherwise it ended, the unit died or
        // the player gave it an order of their own: not ours any more.
        if (!InUnitArray(w, u) || Field<uint32_t>(u, kOffSerial) != c.serial || !IsActive(u) || OrderOf(u) != c.order ||
            Field<int16_t>(u, kOffOrderX) != c.x || Field<int16_t>(u, kOffOrderY) != c.y)
            continue;
        const char* why = StopReason(w, c);
        if (!why) {
            g_channels[kept++] = c;
            continue;
        }
        if (!OnMap(w, X(u), Y(u))) continue;
        IssueOrder(u, static_cast<int16_t>(X(u)), static_cast<int16_t>(Y(u)), nullptr, kRvaStopHandler);
        if (config::g.logCasts)
            logx::Write("channel stopped: %s by caster type %u at %d,%d on tile %d,%d: %s",
                        c.order == kOrderBlizzard ? "blizzard" : "death_and_decay", TypeOf(u), X(u), Y(u), c.x, c.y, why);
    }
    g_channelCount = kept;
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

// Per caster: the spells that existed first keep their priority; the newer ones follow in the game AI's own order.
void CasterThink(const World& w, Unit* caster) {
    switch (Field<uint8_t>(caster, kOffType)) {
    case kTypePaladin:
    case kTypePaladinHero:
        TryCast(w, caster, kSpellHeal) || TryCast(w, caster, kSpellExorcism) || TryHolyVision(w, caster);
        break;
    case kTypeOgreMage:
    case kTypeOgreMageHero:
        TryCast(w, caster, kSpellBloodlust) || TryRunes(w, caster);
        break;
    case kTypeMage:
    case kTypeMageHero:
        // Game AI (FUN_004cb480): polymorph, fireball, invisibility, blizzard, flame shield, slow.
        TryCast(w, caster, kSpellPolymorph) || TryCast(w, caster, kSpellSlow) || TryFireball(w, caster) ||
            TryCast(w, caster, kSpellInvisibility) || TryAreaSpell(w, caster, kSpellBlizzard) ||
            TryCast(w, caster, kSpellFlameShield);
        break;
    case kTypeDeathKnight:
    case kTypeDeathKnightHero:
        // The game AI's own priority: raise dead, unholy armor, (death and decay,) death coil, (whirlwind,) haste.
        TryRaiseDead(w, caster) || TryCast(w, caster, kSpellUnholyArmor) || TryCast(w, caster, kSpellDeathCoil) ||
            TryCast(w, caster, kSpellHaste) || TryAreaSpell(w, caster, kSpellDeathAndDecay) ||
            TryAreaSpell(w, caster, kSpellWhirlwind);
        break;
    }
}

void PassImpl(const World& w) {
    CollectFriendlyBuildings(w);
    GuardChannelsImpl(w);
    g_sumsTried = false;

    // Casts already under way, so two casters never pick the same target for the same spell.
    g_claimCount = 0;
    for (unsigned i = 0; i < w.unitCount && g_claimCount < kMaxClaims; ++i) {
        Unit* u = UnitAt(w, i);
        const uint8_t order = OrderOf(u);
        if (Field<uint8_t>(u, kOffOwner) != w.localPlayer || !IsActive(u) || order < kOrderSpellFirst) continue;
        g_claims[g_claimCount++] = {order, Field<Unit*>(u, kOffOrderTarget), Field<int16_t>(u, kOffOrderX),
                                    Field<int16_t>(u, kOffOrderY)};
    }

    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (Field<uint8_t>(u, kOffOwner) != w.localPlayer || !IsActive(u)) continue;
        if (!(w.typeFlags[Field<uint8_t>(u, kOffType)] & kTfCaster)) continue;
        if (Field<uint16_t>(u, kOffInvisTimer) != 0) continue;  // casting would break the player's invisibility
        if (!OrderAllowsAutocast(OrderOf(u))) continue;
        CasterThink(w, u);
    }
}

}  // namespace

void Pass(const game::World& w) { PassImpl(w); }

void GuardChannels(const game::World& w) {
    CollectFriendlyBuildings(w);
    GuardChannelsImpl(w);
}

}  // namespace autocast
