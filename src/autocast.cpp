#include "autocast.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
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
    int buildingHp;    // hit points of the enemy buildings in the blast when it started, 0 = this one is about units
    uint8_t manaAtStart;  // waves delivered so far = (this - mana now) / cost: the engine counts nothing for us
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
// A dry run answers "would this spell cast if the caster had the mana?": the mana test is skipped and NOTHING is
// written - no order, no claim, no channel, no counter, no log line, no per-pass cache. [priority] save_mana uses it
// to decide whether a caster should sit on its mana for a spell higher in its list.
bool g_dryRun = false;

bool ManaOk(Unit* caster, int manaNeeded) { return g_dryRun || Field<uint8_t>(caster, kOffMana) >= manaNeeded; }

bool Ready(Unit* caster, Spell spell, int manaNeeded) {
    if (!config::g.spell[spell]) return false;
    if (!(At<uint32_t>(kRvaSpellsResearched)[OwnerOf(caster)] & kSpells[spell].researchBit)) return false;
    return ManaOk(caster, manaNeeded);
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
bool FriendlyInDanger(const World& w, int x, int y, int radius, Unit* spared, int walls, Unit** who = nullptr) {
    if (who) *who = nullptr;
    Unit* found = nullptr;
    if (ScanTileRaw(w, x, y, radius, [&](Unit* u) {
            if (u == spared || !CanBeHurt(u) || !IsFriend(w, u)) return false;
            found = u;
            return true;
        })) {
        if (who) *who = found;
        return true;
    }
    for (int i = 0; i < g_footprintCount; ++i) {
        const Footprint& b = g_footprints[i];
        if (b.unit != spared && x >= b.x0 - radius && x <= b.x1 + radius && y >= b.y0 - radius && y <= b.y1 + radius) {
            if (who) *who = b.unit;
            return true;
        }
    }
    return WallNear(w, x, y, walls);  // a wall has no unit: the caller says "a wall"
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

// What a Blizzard / Death and Decay / Whirlwind blast around x, y would find: enemy units, enemy buildings (once
// each, whatever their footprint) and the hit points those buildings have left.
struct AreaTargets {
    int units;
    int buildings;
    int buildingHp;
};

AreaTargets ScanArea(const World& w, uint8_t me, int x, int y, int radius) {
    AreaTargets a{};
    Unit* seen[64];
    int n = 0;
    ScanTileRaw(w, x, y, radius, [&](Unit* u) {
        if (!IsTarget(w, me, u)) return false;
        for (int i = 0; i < n; ++i)
            if (seen[i] == u) return false;
        if (n < 64) seen[n++] = u;
        if (w.typeFlags[TypeOf(u)] & kTfBuilding) {
            ++a.buildings;
            a.buildingHp += Field<uint16_t>(u, kOffHp);
        } else {
            ++a.units;
        }
        return false;
    });
    return a;
}

// A building is worth area_building_value units: it cannot walk out of the blast, which is the whole point.
int AreaValue(const AreaTargets& a) { return a.buildings * config::g.areaBuildingValue + a.units; }

// Worth a channel: one enemy building is enough, otherwise it takes area_min_enemies units. A building plus a unit
// is a valid target. Whirlwind keeps the old rule (it wanders off at random, so a lone building is a waste of 100
// mana): anything in the blast counts as one.
bool AreaGateMet(const AreaTargets& a, bool channel) {
    if (channel) return a.buildings > 0 || a.units >= config::g.areaMinEnemies;
    return a.units + a.buildings >= config::g.areaMinEnemies;
}

// What one wave takes off a structure at the tile the mod aims at, from docs/research/spells.md section 3:
//
//   Blizzard    5 chains x 11 impacts, each chain at "order tile + rand 0..4 tiles - 1.5"
//   D and D     5 clouds x 10 pulses, each cloud at "order tile +/- 2 tiles"
//   per impact  full damage within ~22 px of the unit's centre, a quarter within ~42 px, then h + rand % (h + 1)
//               with h = (dmg + 1) / 2, so a full hit averages ~0.75 x the damage byte
//
// Working that through: about a sixth of the blizzard chains land within full-damage range of a structure at the aim
// point (11 impacts x 0.75 x dmg each), and the death and decay clouds contribute a smaller full share plus a ring of
// quarter hits. That gives roughly 6.6 x dmg per blizzard wave and 4.5 x dmg per death and decay wave; the mod uses
// 5 x the LIVE damage byte for both, which the [spell_damage] section can double. The spread between the two spells
// rests on where a building's centre pixel sits relative to the tile the mod aims at, which is [unverified], and the
// number only ever decides whether to spend one more 25 mana wave, so one constant is honest enough. It is an
// average: a single wave can roll well above or below it.
constexpr int kWavesPerDamagePoint = 5;

int WaveDamage(uint8_t order) {
    const uint32_t rva = order == kOrderBlizzard ? kRvaBlizzardDamageInsn : kRvaDeathAndDecayDamageInsn;
    return kWavesPerDamagePoint * At<uint8_t>(rva)[3];  // the imm8 of `mov byte [reg+0x37], dmg`, live
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

constexpr unsigned kMaxNoteSlots = 2048;  // one diagnostic / cooldown slot per unit array entry
uint32_t g_playMs = 0;                    // play time in ms, the clock every throttle and cooldown uses

// [heal] cooldown_seconds: one timer per caster, shared by Heal and Exorcism, in play time. A caster may break it
// for a heal target at or below urgent_below_percent of its maximum hit points, or for an exorcism the caster's
// mana can finish outright (the live per-hit-point cost from the game's table, so [spell_cost] is honoured).
struct CastNote {
    uint32_t serial;
    uint32_t lastMs;
    bool used;
};
CastNote g_healNotes[kMaxNoteSlots];

unsigned NoteSlot(const World& w, Unit* u) {
    return static_cast<unsigned>((reinterpret_cast<uintptr_t>(u) - reinterpret_cast<uintptr_t>(w.units)) / kUnitSize);
}

bool IsHealSpell(Spell spell) { return spell == kSpellHeal || spell == kSpellExorcism; }

// Why this cast is allowed although the timer is still running, or nullptr when it is not.
const char* UrgentReason(Unit* caster, Spell spell, Unit* target, char* out, size_t outLen) {
    if (spell == kSpellHeal) {
        const int hp = Field<uint16_t>(target, kOffHp), max = At<uint16_t>(kRvaMaxHpByType)[TypeOf(target)];
        if (max <= 0 || hp * 100 > max * config::g.healUrgentBelowPercent) return nullptr;
        sprintf_s(out, outLen, " (urgent, %d %%)", max > 0 ? hp * 100 / max : 0);
        return out;
    }
    // Exorcism turns mana into damage at a fixed price per hit point (FUN_004e2ac4 divides by the cost table entry).
    const int cost = At<uint16_t>(kRvaManaCostByOrder)[kSpells[kSpellExorcism].order];
    const int hp = Field<uint16_t>(target, kOffHp), mana = Field<uint8_t>(caster, kOffMana);
    if (cost <= 0 || hp * cost > mana) return nullptr;
    sprintf_s(out, outLen, " (urgent, %d hp for %d mana)", hp, hp * cost);
    return out;
}

// True when the caster may cast now. `note` is filled with the log suffix of a cooldown-breaking cast.
bool CooldownAllows(const World& w, Unit* caster, Spell spell, Unit* target, char* note, size_t noteLen) {
    *note = 0;
    if (!IsHealSpell(spell) || config::g.healCooldownSeconds <= 0) return true;
    const unsigned slot = NoteSlot(w, caster);
    if (slot >= kMaxNoteSlots) return true;
    const CastNote& n = g_healNotes[slot];
    const uint32_t serial = Field<uint32_t>(caster, kOffSerial);
    if (!n.used || n.serial != serial) return true;
    if (g_playMs - n.lastMs >= static_cast<uint32_t>(config::g.healCooldownSeconds) * 1000) return true;
    return UrgentReason(caster, spell, target, note, noteLen) != nullptr;
}

void StartCooldown(const World& w, Unit* caster, Spell spell) {
    if (!IsHealSpell(spell) || config::g.healCooldownSeconds <= 0) return;
    const unsigned slot = NoteSlot(w, caster);
    if (slot < kMaxNoteSlots) g_healNotes[slot] = {Field<uint32_t>(caster, kOffSerial), g_playMs, true};
}

bool TryCast(const World& w, Unit* caster, Spell spell) {
    if (!config::g.spell[spell]) return false;
    const SpellDef& def = kSpells[spell];
    const uint8_t me = Field<uint8_t>(caster, kOffOwner);
    if (!(At<uint32_t>(kRvaSpellsResearched)[me] & def.researchBit)) return false;
    if (!ManaOk(caster, At<uint16_t>(kRvaManaCostByOrder)[def.order])) return false;

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
    char urgent[64] = "";
    if (!CooldownAllows(w, caster, spell, best, urgent, sizeof(urgent))) return false;
    if (g_dryRun) return true;

    IssueSpell(caster, def.order, 0, 0, best);
    if (OrderOf(caster) != def.order) return false;  // order was not interruptible
    if (g_claimCount < kMaxClaims) g_claims[g_claimCount++] = {def.order, best, 0, 0};
    ++g_castCount;
    StartCooldown(w, caster, spell);
    if (config::g.logCasts)
        logx::Write("cast %s: caster type %u at %d,%d -> target type %u owner %u at %d,%d%s", config::kSpellKeys[spell],
                    Field<uint8_t>(caster, kOffType), Field<int16_t>(caster, kOffX), Field<int16_t>(caster, kOffY),
                    Field<uint8_t>(best, kOffType), Field<uint8_t>(best, kOffOwner), Field<int16_t>(best, kOffX),
                    Field<int16_t>(best, kOffY), urgent);
    return true;
}

// Every tile comes from a live unit or from arithmetic kept inside the map; checked once more here, so no positional
// cast ever depends on the off-map guard in game::IssueOrder.
bool CastAtTile(const World& w, Unit* caster, Spell spell, int x, int y, int enemies) {
    if (!OnMap(w, x, y)) return false;
    if (g_dryRun) return true;
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

// Raise Dead, the computer's rule (FUN_004cac80: research bit 0x2000, mana, a corpse within the 31 x 31 tiles around the
// death knight, whatever [autocast] search_radius says, no enemy needed), with the search done where corpses really are.
// A dying unit is taken off its grid tile (FUN_004ee380 -> FUN_004b5000) and the step action that then turns the same
// slot into type 0x69 (FUN_004bdfc0) never files it back, so no corpse is ever in a unit grid: the computer's own search
// (FUN_004cb3e0, ground grid) finds none. The spell's hit-frame action (FUN_004e2420) walks the unit array instead, and so
// does this. Cast at the corpse's own tile (a unit's tile, on the map). The computer marks the corpse (+0x4C |= 0x20); the
// mod claims the tile instead and writes nothing into the unit. The nearest corpse wins.
constexpr int kRaiseDeadBox = 15;         // FUN_004cb3e0: x-15 .. x+15, y-15 .. y+15
constexpr int kRaiseDeadReach2 = 0x25;    // FUN_004e2420 raises every corpse with dx*dx + dy*dy < 0x25 around its tile
constexpr uint32_t kRaiseNoteEveryMs = 30000;

// A raise already under way (the player's or the mod's) takes every corpse within its reach.
bool RaiseDeadClaimed(int x, int y) {
    for (int i = 0; i < g_claimCount; ++i) {
        const Claim& c = g_claims[i];
        const int dx = c.x - x, dy = c.y - y;
        if (c.order == kSpells[kSpellRaiseDead].order && !c.target && dx * dx + dy * dy < kRaiseDeadReach2) return true;
    }
    return false;
}

// With log_casts on: why a death knight did not raise the dead, once per death knight per 30 s of play.
struct RaiseNote {
    uint32_t serial;
    uint32_t lastMs;
    bool logged;
};
RaiseNote g_raiseNotes[kMaxNoteSlots];
unsigned g_raiseNoteCount = 0;
char g_lastRaiseNote[160] = "";

void NoteRaiseDead(const World& w, Unit* caster, const char* fmt, ...) {
    if (!config::g.logCasts || g_dryRun) return;
    const unsigned slot = static_cast<unsigned>((reinterpret_cast<uintptr_t>(caster) - reinterpret_cast<uintptr_t>(w.units)) / kUnitSize);
    if (slot >= kMaxNoteSlots) return;
    RaiseNote& n = g_raiseNotes[slot];
    const uint32_t serial = Field<uint32_t>(caster, kOffSerial);
    if (n.logged && n.serial == serial && g_playMs - n.lastMs < kRaiseNoteEveryMs) return;
    n = {serial, g_playMs, true};
    va_list args;
    va_start(args, fmt);
    vsprintf_s(g_lastRaiseNote, fmt, args);
    va_end(args);
    ++g_raiseNoteCount;
    logx::Write("raise_dead not cast: death knight at %d,%d mana %u: %s", X(caster), Y(caster), Field<uint8_t>(caster, kOffMana),
                g_lastRaiseNote);
}

bool TryRaiseDead(const World& w, Unit* caster) {
    const SpellDef& def = kSpells[kSpellRaiseDead];
    if (!config::g.spell[kSpellRaiseDead]) {
        NoteRaiseDead(w, caster, "switched off in [spells]");
        return false;
    }
    const uint8_t me = Field<uint8_t>(caster, kOffOwner);
    const uint32_t known = At<uint32_t>(kRvaSpellsResearched)[me];
    if (!(known & def.researchBit)) {
        // A new map starts every player on 0x4020 (fireball, death coil; FUN_004d2b40 at 0x4D2B9E); Raise Dead comes from
        // research (FUN_004acbc0) or from the map's ALOW section (FUN_004d19d0).
        NoteRaiseDead(w, caster, "not researched (known spells 0x%08X)", known);
        return false;
    }
    const int cost = At<uint16_t>(kRvaManaCostByOrder)[def.order];
    if (Field<uint8_t>(caster, kOffMana) < cost) {
        NoteRaiseDead(w, caster, "mana below the cost of %d", cost);
        return false;
    }

    const uint16_t* sq = *At<uint16_t*>(kRvaSquareFlags);
    Unit* best = nullptr;
    int bestDistance = 1 << 30, onMap = 0, inBox = 0, onWater = 0, claimed = 0;
    const int cx = X(caster), cy = Y(caster);
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* t = UnitAt(w, i);
        if (TypeOf(t) != kTypeCorpse) continue;
        // State 2 from death on (FUN_004ee380 sets it, FUN_004bdfc0 keeps it); +8 = raised already (FUN_004e2420) or gone.
        const uint8_t state = Field<uint8_t>(t, kOffStateFlags);
        if ((state & 0x0F) != kStateDying) continue;
        const int x = X(t), y = Y(t);
        if (!OnMap(w, x, y)) continue;
        ++onMap;
        if (abs(x - cx) > kRaiseDeadBox || abs(y - cy) > kRaiseDeadBox) continue;
        // A sunk ship becomes type 0x69 too (0x8C1208: 3 for every ship): never aim at a wreck in the water.
        if (sq && (sq[y * w.mapSize + x] & kSqWater)) {
            ++onWater;
            continue;
        }
        ++inBox;
        if (RaiseDeadClaimed(x, y)) {
            ++claimed;
            continue;
        }
        const int d = Distance(caster, t);
        if (d < bestDistance) {
            bestDistance = d;
            best = t;
        }
    }
    if (!best) {
        if (inBox) NoteRaiseDead(w, caster, "all %d corpses within 15 tiles are claimed by a raise under way", claimed);
        else NoteRaiseDead(w, caster, "no corpse within 15 tiles (%d on the map, %d wrecks on water in reach)", onMap, onWater);
        return false;
    }

    const int16_t x = Field<int16_t>(best, kOffX), y = Field<int16_t>(best, kOffY);
    if (g_dryRun) return true;
    IssueSpell(caster, def.order, x, y, nullptr);
    if (OrderOf(caster) != def.order) {
        NoteRaiseDead(w, caster, "the game kept order %u instead", OrderOf(caster));
        return false;
    }
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

void RememberChannel(Unit* caster, uint8_t order, int x, int y, int buildingHp) {
    const Channel c = {caster,
                       Field<uint32_t>(caster, kOffSerial),
                       order,
                       static_cast<int16_t>(x),
                       static_cast<int16_t>(y),
                       buildingHp,
                       Field<uint8_t>(caster, kOffMana)};
    for (int i = 0; i < g_channelCount; ++i)
        if (g_channels[i].caster == caster) {
            g_channels[i] = c;
            return;
        }
    if (g_channelCount < kMaxChannels) g_channels[g_channelCount++] = c;
}

// With log_casts on: why an area spell was not cast, once per caster per 30 s of play. The reasons are the gates
// below, in the order they are tested.
RaiseNote g_areaNotes[kMaxNoteSlots];

void NoteArea(const World& w, Unit* caster, Spell spell, const char* fmt, ...) {
    if (!config::g.logCasts || g_dryRun) return;
    const unsigned slot =
        static_cast<unsigned>((reinterpret_cast<uintptr_t>(caster) - reinterpret_cast<uintptr_t>(w.units)) / kUnitSize);
    if (slot >= kMaxNoteSlots) return;
    RaiseNote& n = g_areaNotes[slot];
    const uint32_t serial = Field<uint32_t>(caster, kOffSerial);
    if (n.logged && n.serial == serial && g_playMs - n.lastMs < kRaiseNoteEveryMs) return;
    n = {serial, g_playMs, true};
    char why[160];
    va_list args;
    va_start(args, fmt);
    vsprintf_s(why, fmt, args);
    va_end(args);
    logx::Write("%s not cast: caster type %u at %d,%d mana %u: %s", config::kSpellKeys[spell], TypeOf(caster), X(caster),
                Y(caster), Field<uint8_t>(caster, kOffMana), why);
}

// How far the aim tile may sit from the tile that must be hit. The waves scatter over a fixed pattern around the aim
// tile, so a target anywhere inside that pattern is hit about as often as one in the middle:
//   Death and decay (FUN_004af500): cloud = aim tile centre + (rand()%5 << 5) - 0x40 px = -64..+64 px = -2..+2 tiles,
//     centred on the aim tile, so the aim may sit 2 tiles either side of the target.
//   Blizzard (FUN_004aec70): chain = aim tile centre + (rand()%5 << 5) - 0x30 px = -48..+80 px = -1.5..+2.5 tiles.
//     That pattern is half a tile PAST the aim tile: full damage (within ~22 px of a tile centre) still reaches a
//     target 2 tiles before and 3 tiles past the aim, so the aim may sit 3 tiles before and 2 tiles past the target.
struct AimSpan {
    int lo, hi;
};
AimSpan SpanFor(uint8_t order) { return order == kOrderBlizzard ? AimSpan{-3, 2} : AimSpan{-2, 2}; }

// Aim tiles searched per caster per pass once the straight aim was blocked by a friendly: two targets' worth of the
// 6x6 / 5x5 pattern. A pass runs every [general] interval_ticks steps and each aim reads 5x5 grid tiles, so this is
// a few thousand reads in the worst case, and the nearest blocked targets are the ones worth the search anyway.
constexpr int kAimSearchBudget = 64;

enum AimResult { kAimOk, kAimOffMap, kAimOutOfRange, kAimGate, kAimOverkill, kAimClaimed, kAimFriendly, kAimWorse };

// Blizzard, Death and Decay (channelled) and Whirlwind. The aim tile is the spot whose blast is worth the most:
// buildings count area_building_value each, units one each, nearest wins a tie. When the straight aim would catch
// something of the player's, the mod walks the aim off the target - the scatter pattern still covers it - instead of
// giving the cast up, which is what kept these spells off the field whenever his own army was in contact.
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
    if (!Ready(caster, spell, need)) {
        if (channel && config::g.spell[spell] &&
            (At<uint32_t>(kRvaSpellsResearched)[OwnerOf(caster)] & kSpells[spell].researchBit) &&
            Field<uint8_t>(caster, kOffMana) < need)
            NoteArea(w, caster, spell, "%s mana (needs %d)", config::g.priority.saveMana ? "saving" : "not enough", need);
        return false;
    }
    if (spell == kSpellWhirlwind && WhirlwindInFlight(caster)) return false;  // one whirlwind per caster at a time
    const int clearance = channel ? config::g.areaFriendlyClearance : kWhirlwindClearance;
    const int walls = channel ? kChannelWalls : kWhirlwindClearance;
    const int reach = Reach(order);
    const uint8_t me = OwnerOf(caster);
    const int wave = channel ? WaveDamage(order) : 0;
    const Size* sizes = At<Size>(kRvaUnitSizeByType);
    Unit* best = nullptr;
    int bestScore = 0, bestDistance = 1 << 30, bestBuildingHp = 0, bestX = 0, bestY = 0;
    int budget = kAimSearchBudget;
    // Why nothing was cast, for the log line at the end.
    bool sawTarget = false, gateMet = false, blockedFriendly = false, blockedClaim = false, blockedOverkill = false;
    int bestGateValue = 0;
    Unit* witness = nullptr;
    int witnessX = 0, witnessY = 0;

    auto tryAim = [&](Unit* t, int ax, int ay) {
        if (!OnMap(w, ax, ay)) return kAimOffMap;
        // The aim tile itself must be in range: an order further away would send the caster walking, and the
        // friendly-fire check it passed here would be stale by the time it arrived. [unverified] what the engine
        // does with an out-of-range spell order; the mod never issues one.
        const int dx = abs(ax - X(caster)), dy = abs(ay - Y(caster));
        const int d = dx > dy ? dx : dy;
        if (d > reach) return kAimOutOfRange;
        const AreaTargets a = ScanArea(w, me, ax, ay, kAreaCount);
        if (!AreaGateMet(a, channel)) {
            const int value = AreaValue(a);
            if (value > bestGateValue) bestGateValue = value;
            return kAimGate;
        }
        gateMet = true;
        // No overkill: what one wave would already flatten is not worth a channel, unless the units in the blast
        // are reason enough on their own. Units are never in this sum, they walk out of it.
        if (channel && a.buildings > 0 && a.buildingHp <= wave && a.units < config::g.areaMinEnemies) {
            blockedOverkill = true;
            return kAimOverkill;
        }
        if (AreaSpellNear(ax, ay, clearance)) {
            blockedClaim = true;
            return kAimClaimed;
        }
        Unit* who = nullptr;
        if (FriendlyInDanger(w, ax, ay, clearance, caster, walls, &who)) {
            blockedFriendly = true;
            if (who) {
                witness = who;
                witnessX = X(who);
                witnessY = Y(who);
            }
            return kAimFriendly;
        }
        const int value = AreaValue(a);
        if (value < bestScore || (value == bestScore && d >= bestDistance)) return kAimWorse;
        best = t;
        bestScore = value;
        bestDistance = d;
        bestBuildingHp = a.buildingHp;
        bestX = ax;
        bestY = ay;
        return kAimOk;
    };

    ScanGrid(w, caster, reach, [&](Unit* t) {
        if (!IsTarget(w, me, t)) return false;
        sawTarget = true;
        // A building is filed by its top-left tile, but the splash measures from its CENTRE (section 2.6a of
        // docs/research/autocast_all_spells.md): aiming a 4x4 keep at its corner throws most of the wave past it.
        const Size s = (w.typeFlags[TypeOf(t)] & kTfBuilding) ? sizes[TypeOf(t)] : Size{1, 1};
        const int tx = X(t) + (s.w ? s.w - 1 : 0) / 2, ty = Y(t) + (s.h ? s.h - 1 : 0) / 2;
        // The straight aim first: it is the one that hits hardest and it is almost always the one that is taken.
        if (tryAim(t, tx, ty) != kAimFriendly || !channel) return false;
        // Something of the player's is standing in the way. The scatter pattern is wide enough to keep hitting this
        // target from a tile or two further off, so walk the aim around it and take the best spot that is clear.
        const AimSpan span = SpanFor(order);
        for (int oy = span.lo; oy <= span.hi && budget > 0; ++oy)
            for (int ox = span.lo; ox <= span.hi && budget > 0; ++ox) {
                if (!ox && !oy) continue;
                --budget;
                tryAim(t, tx + ox, ty + oy);
            }
        return false;
    });

    if (!best) {
        if (!sawTarget) NoteArea(w, caster, spell, "no enemy in reach");
        else if (!gateMet)
            NoteArea(w, caster, spell, "the best spot is worth %d: it takes one building or %d units", bestGateValue,
                     config::g.areaMinEnemies);
        else if (blockedFriendly && witness)
            NoteArea(w, caster, spell, "every spot worth casting on has your own type %u at %d,%d within %d tiles",
                     TypeOf(witness), witnessX, witnessY, clearance);
        else if (blockedFriendly)
            NoteArea(w, caster, spell, "every spot worth casting on has a wall of yours within %d tiles", walls);
        else if (blockedClaim)
            NoteArea(w, caster, spell, "another area spell is already on the spot");
        else if (blockedOverkill)
            NoteArea(w, caster, spell, "the buildings there would die to one wave");
        return false;
    }
    if (!CastAtTile(w, caster, spell, bestX, bestY, bestScore)) return false;
    if (channel && !g_dryRun) RememberChannel(caster, order, bestX, bestY, bestBuildingHp);
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
    if (FriendlyInDanger(w, c.x, c.y, config::g.areaFriendlyClearance, c.caster, -1))
        return "a friendly unit or building is in the area";
    if (CountEnemies(w, w.localPlayer, c.x, c.y, kChannelReach, false) == 0) return "no enemy left in reach";
    if (config::g.channelManaReserve > 0 && Field<uint8_t>(c.caster, kOffMana) < config::g.channelManaReserve)
        return "mana below channel_mana_reserve";
    // No overkill on buildings. A channel started for buildings runs until the waves it has paid for cover the hit
    // points those buildings had, or until what is left in the blast would die to the damage already falling on it.
    // A channel started for UNITS (buildingHp 0) is never stopped here: units walk in and out, there is nothing to
    // count. Waves are counted by mana, because the engine keeps no count of its own.
    if (c.buildingHp > 0) {
        const AreaTargets a = ScanArea(w, w.localPlayer, c.x, c.y, kAreaCount);
        if (a.units < config::g.areaMinEnemies) {
            const int wave = WaveDamage(c.order), cost = ManaCost(c.order);
            const int spent = c.manaAtStart - Field<uint8_t>(c.caster, kOffMana);
            const int waves = cost > 0 ? spent / cost : 0;
            if (a.buildings == 0 || a.buildingHp <= wave || waves * wave >= c.buildingHp)
                return "the buildings in the area are covered by the waves already cast";
        }
    }
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
int KindOf(uint8_t type) {
    switch (type) {
    case kTypePaladin: case kTypePaladinHero: return kCasterPaladin;
    case kTypeMage: case kTypeMageHero: return kCasterMage;
    case kTypeOgreMage: case kTypeOgreMageHero: return kCasterOgreMage;
    case kTypeDeathKnight: case kTypeDeathKnightHero: return kCasterDeathKnight;
    default: return -1;  // Eye of Kilrogg has its own rule in eye.cpp and is in no priority list
    }
}

bool TrySpell(const World& w, Unit* caster, int spell) {
    switch (spell) {
    case kSpellHolyVision: return TryHolyVision(w, caster);
    case kSpellFireball: return TryFireball(w, caster);
    case kSpellRunes: return TryRunes(w, caster);
    case kSpellRaiseDead: return TryRaiseDead(w, caster);
    case kSpellBlizzard: case kSpellDeathAndDecay: case kSpellWhirlwind:
        return TryAreaSpell(w, caster, static_cast<Spell>(spell));
    default: return TryCast(w, caster, static_cast<Spell>(spell));
    }
}

// The mana a spell asks for before it even looks for a target. The two channels want three waves (the computer's own
// rule) and never less than channel_mana_reserve plus one wave; everything else wants its price.
int ManaNeed(int spell) {
    const int cost = ManaCost(kSpells[spell].order);
    if (spell != kSpellBlizzard && spell != kSpellDeathAndDecay) return cost;
    const int three = 3 * cost, reserve = config::g.channelManaReserve + cost;
    return three > reserve ? three : reserve;
}

// Would this spell cast if only the caster had the mana? Everything else is tested for real: research, the switch in
// [spells], a target, the friendly-fire clearance, claims, the area gates, the overkill rule. Nothing is written.
bool WouldCastWithMoreMana(const World& w, Unit* caster, int spell) {
    if (!config::g.spell[spell]) return false;                          // off: there is nothing to save up for
    if (Field<uint8_t>(caster, kOffMana) >= ManaNeed(spell)) return false;  // it had the mana and still did not cast
    const bool sumsTried = g_sumsTried, sumsReady = g_sumsReady;        // holy vision's per-pass cache stays untouched
    g_dryRun = true;
    const bool would = TrySpell(w, caster, spell);
    g_dryRun = false;
    g_sumsTried = sumsTried;
    g_sumsReady = sumsReady;
    return would;
}

// One line per caster per 30 s of play while it is saving up, keyed by the unit's creation serial.
struct SaveNote {
    uint32_t serial;
    uint32_t lastMs;
    bool logged;
};
SaveNote g_saveNotes[kMaxNoteSlots];

void NoteSaving(const World& w, Unit* caster, int kind, int spell) {
    if (!config::g.logCasts) return;
    const unsigned slot =
        static_cast<unsigned>((reinterpret_cast<uintptr_t>(caster) - reinterpret_cast<uintptr_t>(w.units)) / kUnitSize);
    if (slot >= kMaxNoteSlots) return;
    SaveNote& n = g_saveNotes[slot];
    const uint32_t serial = Field<uint32_t>(caster, kOffSerial);
    if (n.logged && n.serial == serial && g_playMs - n.lastMs < kRaiseNoteEveryMs) return;
    n = {serial, g_playMs, true};
    logx::Write("saving: %s at %d,%d mana %u for %s (needs %d)", config::kCasterKindKeys[kind], X(caster), Y(caster),
                Field<uint8_t>(caster, kOffMana), config::kSpellKeys[spell], ManaNeed(spell));
}

// [priority]: the caster walks its own list. With save_mana on, a spell it could cast except for the mana stops the
// walk: the caster keeps its mana for it instead of spending it on something further down the list.
void CasterThink(const World& w, Unit* caster) {
    const int kind = KindOf(Field<uint8_t>(caster, kOffType));
    if (kind < 0) return;
    const int8_t* list = config::g.priority.list[kind];
    for (int i = 0; i < kSpellCount && list[i] >= 0; ++i) {
        const int spell = list[i];
        if (TrySpell(w, caster, spell)) return;
        if (!config::g.priority.saveMana) continue;
        if (WouldCastWithMoreMana(w, caster, spell)) {
            NoteSaving(w, caster, kind, spell);
            return;
        }
    }
}

void PassImpl(const World& w) {
    CollectFriendlyBuildings(w);
    g_sumsTried = false;

    // Casts already under way, so two casters never pick the same target for the same spell. This is taken BEFORE the
    // watchdog stops anything: a channel it ends keeps its tile claimed for the rest of the pass, so the caster it
    // just freed does not aim the same spell at the same tile again a few lines below.
    g_claimCount = 0;
    for (unsigned i = 0; i < w.unitCount && g_claimCount < kMaxClaims; ++i) {
        Unit* u = UnitAt(w, i);
        const uint8_t order = OrderOf(u);
        if (Field<uint8_t>(u, kOffOwner) != w.localPlayer || !IsActive(u) || order < kOrderSpellFirst) continue;
        g_claims[g_claimCount++] = {order, Field<Unit*>(u, kOffOrderTarget), Field<int16_t>(u, kOffOrderX),
                                    Field<int16_t>(u, kOffOrderY)};
    }
    GuardChannelsImpl(w);

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

void AddPlayTime(unsigned ms) { g_playMs += ms; }

void OnNewMap() {
    g_playMs = 0;
    memset(g_healNotes, 0, sizeof(g_healNotes));
    memset(g_raiseNotes, 0, sizeof(g_raiseNotes));
    memset(g_areaNotes, 0, sizeof(g_areaNotes));
    memset(g_saveNotes, 0, sizeof(g_saveNotes));
    g_channelCount = 0;
}
unsigned RaiseDeadNoteCount() { return g_raiseNoteCount; }
unsigned CastCount() { return g_castCount; }
unsigned ChannelCount() { return static_cast<unsigned>(g_channelCount); }
const char* LastRaiseDeadNote() { return g_lastRaiseNote; }

void GuardChannels(const game::World& w) {
    CollectFriendlyBuildings(w);
    GuardChannelsImpl(w);
}

}  // namespace autocast
