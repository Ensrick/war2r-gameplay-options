#include "dodge.h"

#include <cstdlib>
#include <cstring>

#include "config.h"
#include "log.h"

using namespace game;

namespace dodge {

namespace {

// ---- Where the areas are (docs/research/dodge.md) ----
// Blizzard shard, missile type 5 (FUN_004aec70): +0x28 / +0x2A = the pixel it falls on (one of the 5x5 tile centres
// around the aim, FUN_004af040), +0x30 = the caster. The aim tile itself is the caster's order tile (+0x84 / +0x86)
// while the caster is still on the channel (order 0x2F).
// Death and decay cloud, type 6 (FUN_004af500): +0x28 / +0x2A = the aim tile's centre (FUN_004af7b0, positional
// order: tile * 32 + 16), +0x00 / +0x02 = where this cloud sits. A cloud is known from the first pulse and gone when
// its counter runs out (FUN_004aeb30 frees the slot).
constexpr uint8_t kMissileBlizzard = 5, kMissileDecay = 6;
constexpr int kMisOffTargetX = 0x28, kMisOffTargetY = 0x2A;
constexpr int kPattern = 2;   // impacts on aim -2 .. aim +2
constexpr int kRing = 1;      // a quarter hit reaches 42 px: a unit on the tile next to an impact tile is still hit
constexpr int kSafeExtra = 1; // a destination (or a place to go back to) keeps one more tile: no ping-pong at the edge
constexpr int kMaxAreas = 64;
constexpr int kSearch = 10;   // tiles searched around a unit for a safe tile
constexpr unsigned kPassEveryMs = 250;
constexpr uint32_t kClearMs = 1000;       // an area must be gone this long before an order is given back
constexpr uint32_t kForgetMs = 60000;     // a saved order older than this is dropped
constexpr unsigned kMaxSlots = 2048;

struct Area {
    int x0, y0, x1, y1;  // inclusive danger box
    int cx, cy;          // aim (or impact) tile
};
Area g_areas[kMaxAreas];
int g_areaCount = 0;

enum Mode : uint8_t { kIdle, kDodging, kHolding, kReturning };
enum Saved : uint8_t { kSavedNone, kSavedAttackUnit, kSavedAttackMove, kSavedStand, kSavedStop, kSavedPatrol };

struct State {
    uint32_t serial;
    Mode mode;
    Saved saved;
    int16_t destX, destY;      // the move the mod gave (dodge or return), -1 = none
    int16_t homeX, homeY;      // where it stood when it left (stand / stop: where it goes back to)
    int16_t savedX, savedY;    // attack-move / patrol destination
    Unit* target;              // attack target
    uint32_t targetSerial;
    uint32_t sinceMs;          // when the order was saved
    uint32_t dangerMs;         // last time the way back was still in an area
};
State g_state[kMaxSlots];

uint32_t g_clockMs = 0;
unsigned g_sincePassMs = 0;
unsigned g_dodged = 0, g_held = 0, g_restored = 0;

bool InUnitArray(const World& w, Unit* u) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(w.units), p = reinterpret_cast<uintptr_t>(u);
    return u && p >= base && p < base + static_cast<uintptr_t>(w.unitCount) * kUnitSize && (p - base) % kUnitSize == 0;
}

void AddArea(int cx, int cy, int half) {
    for (int i = 0; i < g_areaCount; ++i)
        if (g_areas[i].cx == cx && g_areas[i].cy == cy && g_areas[i].x1 - g_areas[i].x0 >= 2 * half) return;
    if (g_areaCount >= kMaxAreas) return;
    g_areas[g_areaCount++] = {cx - half, cy - half, cx + half, cy + half, cx, cy};
}

void CollectAreas(const World& w) {
    g_areaCount = 0;
    const uint8_t* pool = *At<uint8_t*>(kRvaMissilePool);
    const uint32_t slots = *At<uint32_t>(kRvaMissileSlots);
    if (!pool) return;
    for (uint32_t i = 0; i < slots && i < 4096; ++i) {
        const uint8_t* m = pool + i * kMissileSize;
        const uint8_t type = m[kMisOffType];
        if ((m[kMisOffFlags] & 1) || (type != kMissileBlizzard && type != kMissileDecay)) continue;
        const int tx = *reinterpret_cast<const int16_t*>(m + kMisOffTargetX) >> 5;
        const int ty = *reinterpret_cast<const int16_t*>(m + kMisOffTargetY) >> 5;
        if (type == kMissileDecay) {
            AddArea(tx, ty, kPattern + kRing);
            continue;
        }
        Unit* caster = *reinterpret_cast<Unit* const*>(m + kMisOffSource);
        if (InUnitArray(w, caster) && IsActive(caster) && OrderOf(caster) == kOrderBlizzard)
            AddArea(Field<int16_t>(caster, kOffOrderX), Field<int16_t>(caster, kOffOrderY), kPattern + kRing);
        else
            AddArea(tx, ty, kRing);  // the channel is over: only the chains still falling on their point
    }
}

bool InArea(int x, int y, int extra) {
    for (int i = 0; i < g_areaCount; ++i) {
        const Area& a = g_areas[i];
        if (x >= a.x0 - extra && x <= a.x1 + extra && y >= a.y0 - extra && y <= a.y1 + extra) return true;
    }
    return false;
}

int NearestAreaDistance(int x, int y) {
    int best = 1 << 30;
    for (int i = 0; i < g_areaCount; ++i) {
        const int dx = abs(x - g_areas[i].cx), dy = abs(y - g_areas[i].cy);
        const int d = dx > dy ? dx : dy;
        if (d < best) best = d;
    }
    return best;
}

int Cheb(int ax, int ay, int bx, int by) {
    const int dx = abs(ax - bx), dy = abs(ay - by);
    return dx > dy ? dx : dy;
}

// Can the unit stand there: flyers anywhere; a ship on water; anything else on open land (no forest, rock, wall or
// building: the square bits land units are blocked by, kSq*).
bool Walkable(const World& w, Unit* u, int x, int y) {
    if (x < 0 || y < 0 || x >= w.mapSize || y >= w.mapSize) return false;
    if (w.typeFlags[TypeOf(u)] & kTfFlyer) return true;
    const uint16_t* sq = *At<uint16_t*>(kRvaSquareFlags);
    if (!sq) return true;
    const uint16_t here = sq[Field<int16_t>(u, kOffY) * w.mapSize + Field<int16_t>(u, kOffX)];
    const uint16_t there = sq[y * w.mapSize + x];
    if ((here & kSqWater) != (there & kSqWater)) return false;
    return !(there & (kSqUnpassable | kSqWalls | kSqBuilding));
}

// The nearest tile clear of every area by one more tile than the danger box, away from the area's aim first, then
// toward where the unit wants to be (its target or destination), then the first found.
bool SafeTile(const World& w, Unit* u, int wantX, int wantY, int16_t* outX, int16_t* outY) {
    const int ux = Field<int16_t>(u, kOffX), uy = Field<int16_t>(u, kOffY);
    for (int r = 1; r <= kSearch; ++r) {
        int bestAway = -1, bestWant = 1 << 30;
        for (int y = uy - r; y <= uy + r; ++y)
            for (int x = ux - r; x <= ux + r; ++x) {
                if (Cheb(x, y, ux, uy) != r || InArea(x, y, kSafeExtra) || !Walkable(w, u, x, y)) continue;
                const int away = NearestAreaDistance(x, y);
                const int want = wantX >= 0 ? Cheb(x, y, wantX, wantY) : 0;
                if (away > bestAway || (away == bestAway && want < bestWant)) {
                    bestAway = away;
                    bestWant = want;
                    *outX = static_cast<int16_t>(x);
                    *outY = static_cast<int16_t>(y);
                }
            }
        if (bestAway >= 0) return true;
    }
    return false;
}

// Casters busy with a spell (the channelling mage above all: its own blizzard never hurts it, FUN_004afb50 spares the
// missile's source), units under Unholy Armor (immune, FUN_004bd8f0), buildings, and anything not on the map.
bool Exempt(const World& w, Unit* u) {
    if (w.typeFlags[TypeOf(u)] & kTfBuilding) return true;
    if (OrderOf(u) >= kOrderSpellFirst && OrderOf(u) != kOrderNone) return true;
    if (Field<uint16_t>(u, kOffArmorTimer) != 0) return true;
    const int x = Field<int16_t>(u, kOffX), y = Field<int16_t>(u, kOffY);
    return x < 0 || y < 0 || x >= w.mapSize || y >= w.mapSize;
}

bool IsOurMove(const State& s, Unit* u) {
    return s.destX >= 0 && OrderOf(u) == kOrderMove && Field<int16_t>(u, kOffOrderX) == s.destX &&
           Field<int16_t>(u, kOffOrderY) == s.destY;
}

bool Idle(Unit* u) {
    const uint8_t o = OrderOf(u);
    return o == kOrderStop || o == kOrderStand;
}

// The unit it is going for: the order target, or the one it picked up by itself (+0x54, set by the acquisition code
// FUN_004a8e00 at 0x4A92C5).
Unit* Chasing(const World& w, Unit* u) {
    Unit* t = Field<Unit*>(u, kOffOrderTarget);
    if (!t) t = Field<Unit*>(u, kOffAutoTarget);
    return InUnitArray(w, t) && IsActive(t) ? t : nullptr;
}

void Save(const World& w, State& s, Unit* u) {
    const uint8_t o = OrderOf(u);
    Unit* t = Field<Unit*>(u, kOffOrderTarget);
    s.saved = kSavedStop;
    s.target = nullptr;
    if ((o == kOrderAttack || o == kOrderAttackTarget) && InUnitArray(w, t) && IsActive(t)) {
        s.saved = kSavedAttackUnit;
        s.target = t;
        s.targetSerial = Field<uint32_t>(t, kOffSerial);
    } else if (o == kOrderAttackArea) {
        s.saved = kSavedAttackMove;
        s.savedX = Field<int16_t>(u, kOffOrderX);
        s.savedY = Field<int16_t>(u, kOffOrderY);
    } else if (o == kOrderPatrol || o == kOrderMovePatrol) {
        s.saved = kSavedPatrol;
        s.savedX = Field<int16_t>(u, kOffOrderX);
        s.savedY = Field<int16_t>(u, kOffOrderY);
    } else if (o == kOrderStand) {
        s.saved = kSavedStand;
    } else if (o != kOrderStop) {
        s.saved = kSavedNone;  // work orders (harvest, repair, build): not given back, the unit is left idle
    }
    s.homeX = Field<int16_t>(u, kOffX);
    s.homeY = Field<int16_t>(u, kOffY);
    s.sinceMs = g_clockMs;
    s.dangerMs = g_clockMs;
}

const char* SavedName(Saved k) {
    switch (k) {
    case kSavedAttackUnit: return "attack";
    case kSavedAttackMove: return "attack-move";
    case kSavedStand: return "stand ground";
    case kSavedStop: return "stop";
    case kSavedPatrol: return "patrol";
    default: return "nothing";
    }
}

// Where giving the order back would take the unit: the target, the old spot, or the next step toward the destination.
bool WayBackInArea(const World& w, const State& s, Unit* u) {
    switch (s.saved) {
    case kSavedAttackUnit:
        if (!InUnitArray(w, s.target) || !IsActive(s.target) || Field<uint32_t>(s.target, kOffSerial) != s.targetSerial) return false;
        return InArea(Field<int16_t>(s.target, kOffX), Field<int16_t>(s.target, kOffY), kSafeExtra);
    case kSavedAttackMove:
    case kSavedPatrol: {
        const int ux = Field<int16_t>(u, kOffX), uy = Field<int16_t>(u, kOffY);
        const int sx = (s.savedX > ux) - (s.savedX < ux), sy = (s.savedY > uy) - (s.savedY < uy);
        for (int k = 1; k <= 3; ++k)
            if (InArea(ux + k * sx, uy + k * sy, 0)) return true;
        return false;
    }
    case kSavedStand:
    case kSavedStop:
        return InArea(s.homeX, s.homeY, kSafeExtra);
    default:
        return false;
    }
}

void Restore(const World& w, State& s, Unit* u) {
    switch (s.saved) {
    case kSavedAttackUnit:
        if (InUnitArray(w, s.target) && IsActive(s.target) && Field<uint32_t>(s.target, kOffSerial) == s.targetSerial)
            IssueOrder(u, 0, 0, s.target, kRvaAttackHandler);
        break;
    case kSavedAttackMove:
        IssueOrder(u, s.savedX, s.savedY, nullptr, kRvaAttackMoveHandler);
        break;
    case kSavedPatrol:
        IssueOrder(u, s.savedX, s.savedY, nullptr, kRvaPatrolCommandHandler);
        break;
    case kSavedStand:
    case kSavedStop:
        if (Cheb(Field<int16_t>(u, kOffX), Field<int16_t>(u, kOffY), s.homeX, s.homeY) > 0) {
            IssueOrder(u, s.homeX, s.homeY, nullptr, kRvaMoveHandler);
            if (OrderOf(u) == kOrderMove) {
                s.mode = kReturning;  // a stand is put back once it is there
                s.destX = s.homeX;
                s.destY = s.homeY;
                ++g_restored;
                return;
            }
        } else if (s.saved == kSavedStand) {
            IssueOrder(u, 0, 0, nullptr, kRvaStandHandler);
        }
        break;
    default:
        break;
    }
    ++g_restored;
    if (config::g.logCasts)
        logx::Write("dodge: unit type %u at %d,%d back to %s", TypeOf(u), Field<int16_t>(u, kOffX), Field<int16_t>(u, kOffY),
                    SavedName(s.saved));
    s.mode = kIdle;
    s.destX = s.destY = -1;
}

void Pass(const World& w) {
    CollectAreas(w);
    for (unsigned i = 0; i < w.unitCount && i < kMaxSlots; ++i) {
        Unit* u = UnitAt(w, i);
        State& s = g_state[i];
        const uint32_t serial = Field<uint32_t>(u, kOffSerial);
        if (OwnerOf(u) != w.localPlayer || !IsActive(u) || Exempt(w, u)) {
            if (s.serial != serial) s.serial = 0;
            continue;
        }
        if (s.serial != serial) {
            memset(&s, 0, sizeof(s));
            s.serial = serial;
            s.destX = s.destY = -1;
        }
        const int x = Field<int16_t>(u, kOffX), y = Field<int16_t>(u, kOffY);
        const uint8_t order = OrderOf(u);

        // The player's own orders win. While the mod has the unit, anything that is not the mod's move, not idle and
        // not the hold it was given came from the player: from then on it is his (the scouts' rule).
        if (s.mode != kIdle) {
            const bool mine = IsOurMove(s, u) || (s.mode == kHolding && order == kOrderStand) ||
                              ((s.mode == kDodging || s.mode == kReturning) && Idle(u));
            if (!mine || g_clockMs - s.sinceMs > kForgetMs) {
                s.mode = kIdle;
                s.destX = s.destY = -1;
                continue;
            }
        } else if (order == kOrderMove || order == kOrderMovePatrol) {
            continue;  // a move of the player's: walking through is his call
        }

        if (s.mode == kReturning) {
            if (IsOurMove(s, u)) continue;
            if (s.saved == kSavedStand) IssueOrder(u, 0, 0, nullptr, kRvaStandHandler);
            s.mode = kIdle;
            s.destX = s.destY = -1;
            continue;
        }

        if (InArea(x, y, 0)) {
            if (s.mode == kDodging && IsOurMove(s, u)) continue;  // on the way out
            if (s.mode == kIdle) Save(w, s, u);
            int wantX = -1, wantY = -1;
            if (s.saved == kSavedAttackUnit && s.target) {
                wantX = Field<int16_t>(s.target, kOffX);
                wantY = Field<int16_t>(s.target, kOffY);
            } else if (s.saved == kSavedAttackMove || s.saved == kSavedPatrol) {
                wantX = s.savedX;
                wantY = s.savedY;
            }
            int16_t dx = 0, dy = 0;
            if (!SafeTile(w, u, wantX, wantY, &dx, &dy)) continue;
            IssueOrder(u, dx, dy, nullptr, kRvaMoveHandler);
            if (OrderOf(u) != kOrderMove) continue;
            s.mode = kDodging;
            s.destX = dx;
            s.destY = dy;
            s.dangerMs = g_clockMs;
            ++g_dodged;
            if (config::g.logCasts)
                logx::Write("dodge: unit type %u at %d,%d out of a blizzard / death and decay -> %d,%d (was on %s)", TypeOf(u),
                            x, y, dx, dy, SavedName(s.saved));
            continue;
        }

        if (s.mode == kDodging || s.mode == kHolding) {
            // Out of it. Give the order back once the way back has been clear for a second.
            if (WayBackInArea(w, s, u)) {
                s.dangerMs = g_clockMs;
                continue;
            }
            if (s.mode == kDodging && IsOurMove(s, u)) continue;
            if (g_clockMs - s.dangerMs < kClearMs) continue;
            Restore(w, s, u);
            continue;
        }

        // Avoid: a unit that would walk into an area by itself - chasing a target inside one it cannot reach from
        // here, or an attack-move / patrol whose next steps lie in one - holds its ground at the edge instead.
        bool walksIn = false;
        Unit* t = Chasing(w, u);
        if (t && order != kOrderStand) {
            const int tx = Field<int16_t>(t, kOffX), ty = Field<int16_t>(t, kOffY);
            walksIn = InArea(tx, ty, 0) && Cheb(x, y, tx, ty) > At<uint8_t>(kRvaAttackRangeByType)[TypeOf(u)];
        }
        if (!walksIn && (order == kOrderAttackArea || order == kOrderPatrol)) {
            const int ox = Field<int16_t>(u, kOffOrderX), oy = Field<int16_t>(u, kOffOrderY);
            const int sx = (ox > x) - (ox < x), sy = (oy > y) - (oy < y);
            for (int k = 1; k <= 2 && !walksIn; ++k) walksIn = (sx || sy) && InArea(x + k * sx, y + k * sy, 0);
        }
        if (!walksIn) continue;
        Save(w, s, u);
        IssueOrder(u, 0, 0, nullptr, kRvaStandHandler);
        if (OrderOf(u) != kOrderStand) continue;
        s.mode = kHolding;
        ++g_held;
        if (config::g.logCasts)
            logx::Write("dodge: unit type %u at %d,%d holds at the edge of a blizzard / death and decay (was on %s)", TypeOf(u),
                        x, y, SavedName(s.saved));
    }
}

}  // namespace

void OnTick(const World& w, unsigned elapsedMs) {
    g_clockMs += elapsedMs;
    if (!config::g.dodge.enabled) {
        g_sincePassMs = 0;
        return;
    }
    g_sincePassMs += elapsedMs;
    if (g_sincePassMs < kPassEveryMs) return;
    g_sincePassMs = 0;
    Pass(w);
}

void OnNewMap() {
    memset(g_state, 0, sizeof(g_state));
    g_areaCount = 0;
    g_clockMs = 0;
    g_sincePassMs = 0;
    g_dodged = g_held = g_restored = 0;
}

unsigned AreaCount() { return static_cast<unsigned>(g_areaCount); }
unsigned DodgeCount() { return g_dodged; }
unsigned HoldCount() { return g_held; }
unsigned RestoreCount() { return g_restored; }

}  // namespace dodge
