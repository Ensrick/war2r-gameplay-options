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
    {0x32, 0x02000, false},  // raise dead (cast at a tile, not at a unit)
};

struct Claim {
    uint8_t order;
    Unit* target;  // null for a positional cast, then x/y identify it
    int16_t x, y;
};
constexpr int kMaxClaims = 64;
Claim g_claims[kMaxClaims];
int g_claimCount = 0;

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

// Raise Dead is cast at a corpse's tile. Only while an enemy is around, so the short-lived skeletons get used.
bool TryRaiseDead(const World& w, Unit* caster) {
    if (!config::g.spell[kSpellRaiseDead]) return false;
    const SpellDef& def = kSpells[kSpellRaiseDead];
    const uint8_t me = Field<uint8_t>(caster, kOffOwner);
    if (!(At<uint32_t>(kRvaSpellsResearched)[me] & def.researchBit)) return false;
    if (Field<uint8_t>(caster, kOffMana) < At<uint16_t>(kRvaManaCostByOrder)[def.order]) return false;
    if (!EnemyNear(w, caster, me, config::g.searchRadius)) return false;

    Unit* best = nullptr;
    int bestDistance = 1 << 30;
    ScanGridRaw(w, caster, config::g.searchRadius, [&](Unit* t) {
        if (Field<uint8_t>(t, kOffType) != kTypeCorpse || (Field<uint8_t>(t, kOffStateFlags) & 0x0F) != kStateDying) return false;
        if (IsTileClaimed(def.order, Field<int16_t>(t, kOffX), Field<int16_t>(t, kOffY))) return false;
        const int d = Distance(caster, t);
        if (d < bestDistance) {
            bestDistance = d;
            best = t;
        }
        return false;
    });
    if (!best) return false;

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
        // The game AI's own priority: raise dead, unholy armor, death coil, haste.
        TryRaiseDead(w, caster) || TryCast(w, caster, kSpellUnholyArmor) || TryCast(w, caster, kSpellDeathCoil) ||
            TryCast(w, caster, kSpellHaste);
        break;
    }
}

void PassImpl(const World& w) {
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

}  // namespace autocast
