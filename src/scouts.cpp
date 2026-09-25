#include "scouts.h"

#include <cstring>

#include "config.h"
#include "log.h"

using namespace game;

namespace scouts {

namespace {

// One slot per unit array entry, checked by creation serial (the eye and the workers do the same).
constexpr unsigned kMaxSlots = 2048;
struct ScoutState {
    uint32_t serial;
    int16_t destX, destY;  // the move the mod gave it, -1 = none
    uint32_t idleMs;       // play time spent idle since the player (or nobody) last gave it an order
};
ScoutState g_state[kMaxSlots];

constexpr unsigned kPassEveryMs = 250;
constexpr int kArrived = 2;        // tiles from the destination that count as "got there" (the eye uses the same)
constexpr int kWindow = 4;         // half width of the window scored around a candidate tile: a flyer's sight is larger
constexpr int kSpacing = 10;       // no two scouts are sent within this many tiles of each other's destination
constexpr int kDangerMargin = 2;   // tiles kept between a destination and the attack range of a known anti-air enemy
constexpr int kSamples = 64;       // candidate tiles tried per scout per decision
constexpr int kMaxDanger = 256;
constexpr uint32_t kMaxAgeMs = 10 * 60 * 1000;  // fog older than this is all equally worth a look

unsigned g_sincePassMs = 0;
uint32_t g_clockMs = 0;  // play time since the map started
unsigned g_orders = 0;

// When each tile was last in the local player's sight, in play time. 0 = not since the map (or the mod) started.
uint32_t g_lastSeen[kMaxMapSize * kMaxMapSize];

struct Danger {
    int x, y, radius;
};
Danger g_danger[kMaxDanger];
int g_dangerCount = 0;

uint32_t g_rng = 0x9E3779B9;
uint32_t Rand() {  // xorshift32: the game's RNG is never touched, so the mod does not change the simulation's dice
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int Cheb(int ax, int ay, int bx, int by) {
    const int dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

bool IsScoutType(uint8_t type) { return config::g.scouts.type[type]; }

bool Idle(Unit* u) {
    const uint8_t order = Field<uint8_t>(u, kOffOrder);
    return Field<uint8_t>(u, kOffNextOrder) == kOrderNone && (order == kOrderStop || order == kOrderStand);
}

// The player can see the unit right now: not under his fog (the picking loops' own test, 0x4F2C75).
bool Visible(const World& w, Unit* u) { return !(Field<uint8_t>(u, kOffFogMask) & (1u << (w.localPlayer & 7))); }

// Enemies that can shoot at a flyer: the game's own "can attack an air target" bit, byte table 0x918580 bit 2
// (FUN_004a9810 at 0x4A9825). Only what the player knows about: a unit he sees now, or a building (towers) standing
// on explored ground, which is what his fog shows him too.
void CollectDanger(const World& w) {
    g_dangerCount = 0;
    const uint8_t* canTarget = At<uint8_t>(kRvaCanTargetByType);
    const uint8_t* range = At<uint8_t>(kRvaAttackRangeByType);
    const uint8_t* explored = *At<uint8_t*>(kRvaExploredMap);
    for (unsigned i = 0; i < w.unitCount && g_dangerCount < kMaxDanger; ++i) {
        Unit* u = UnitAt(w, i);
        if (!IsActive(u) || !IsEnemy(w, w.localPlayer, u) || !(canTarget[TypeOf(u)] & kCanTargetAir)) continue;
        const int x = Field<int16_t>(u, kOffX), y = Field<int16_t>(u, kOffY);
        if (x < 0 || y < 0 || x >= w.mapSize || y >= w.mapSize) continue;
        const bool building = (w.typeFlags[TypeOf(u)] & kTfBuilding) != 0;
        const bool known = Visible(w, u) || (building && explored && explored[y * w.mapSize + x] != kTileUnexplored);
        if (!known) continue;
        g_danger[g_dangerCount++] = {x, y, range[TypeOf(u)] + kDangerMargin};
    }
}

bool Dangerous(int x, int y) {
    for (int i = 0; i < g_dangerCount; ++i)
        if (Cheb(x, y, g_danger[i].x, g_danger[i].y) <= g_danger[i].radius) return true;
    return false;
}

// The destination of every other scout the mod is flying, so two of them never head for the same area.
bool Claimed(const World& w, unsigned self, int x, int y) {
    for (unsigned i = 0; i < w.unitCount && i < kMaxSlots; ++i) {
        const ScoutState& s = g_state[i];
        if (i == self || s.destX < 0 || s.serial != Field<uint32_t>(UnitAt(w, i), kOffSerial)) continue;
        if (Cheb(x, y, s.destX, s.destY) < kSpacing) return true;
    }
    return false;
}

int CountUnexplored(const uint8_t* explored, int size, int cx, int cy) {
    int n = 0;
    for (int y = cy - kWindow; y <= cy + kWindow; ++y) {
        if (y < 0 || y >= size) continue;
        for (int x = cx - kWindow; x <= cx + kWindow; ++x)
            if (x >= 0 && x < size && explored[y * size + x] == kTileUnexplored) ++n;
    }
    return n;
}

enum Why { kNone, kUnexplored, kStale };

// Best of kSamples random tiles: never-explored ground first (most of it for the least flying), and once the whole map
// is explored the fogged tile that has gone longest unseen, so the scouts keep patrolling instead of stopping. The
// line to it (sampled at its middle) and the tile itself stay clear of known anti-air.
Why PickDestination(const World& w, unsigned self, int sx, int sy, int16_t* outX, int16_t* outY, uint32_t* outAge) {
    const uint8_t* explored = *At<uint8_t*>(kRvaExploredMap);
    const uint8_t* visible = *At<uint8_t*>(kRvaVisibleMap);
    const int size = w.mapSize;
    int bestFresh = 0, bestStale = 0;
    Why why = kNone;
    for (int k = 0; k < kSamples; ++k) {
        // Half the tries within 24 tiles, the other half anywhere on the map (uniform, so the far corners get a chance).
        const bool near = k < kSamples / 2;
        const int x = near ? Clamp(sx - 24 + static_cast<int>(Rand() % 49), 0, size - 1) : static_cast<int>(Rand() % size);
        const int y = near ? Clamp(sy - 24 + static_cast<int>(Rand() % 49), 0, size - 1) : static_cast<int>(Rand() % size);
        const int d = Cheb(x, y, sx, sy);
        if (d <= kArrived) continue;
        if (Dangerous(x, y) || Dangerous((x + sx) / 2, (y + sy) / 2) || Claimed(w, self, x, y)) continue;
        const int fresh = explored ? CountUnexplored(explored, size, x, y) : 0;
        if (fresh) {
            const int score = fresh * 4 - d + 1000;
            if (score > bestFresh) {
                bestFresh = score;
                why = kUnexplored;
                *outX = static_cast<int16_t>(x);
                *outY = static_cast<int16_t>(y);
            }
            continue;
        }
        if (bestFresh || !visible || visible[y * size + x] != kTileUnexplored) continue;  // 0x10 = fogged right now
        uint32_t age = g_clockMs - g_lastSeen[y * size + x];
        if (age > kMaxAgeMs) age = kMaxAgeMs;
        const int score = static_cast<int>(age / 1000) * 2 - d + 1;
        if (score > bestStale) {
            bestStale = score;
            why = kStale;
            *outX = static_cast<int16_t>(x);
            *outY = static_cast<int16_t>(y);
            *outAge = age;
        }
    }
    return why;
}

void UpdateLastSeen(const World& w) {
    const uint8_t* visible = *At<uint8_t*>(kRvaVisibleMap);
    if (!visible || w.mapSize <= 0 || w.mapSize > kMaxMapSize) return;
    const int n = w.mapSize * w.mapSize;
    for (int i = 0; i < n; ++i)
        if (visible[i] != kTileUnexplored) g_lastSeen[i] = g_clockMs;
}

void Pass(const World& w, unsigned passMs) {
    UpdateLastSeen(w);
    CollectDanger(w);
    const unsigned idleNeeded = static_cast<unsigned>(config::g.scouts.idleSeconds) * 1000;
    for (unsigned i = 0; i < w.unitCount && i < kMaxSlots; ++i) {
        Unit* u = UnitAt(w, i);
        ScoutState& s = g_state[i];
        const uint32_t serial = Field<uint32_t>(u, kOffSerial);
        if (OwnerOf(u) != w.localPlayer || !IsActive(u) || !IsScoutType(TypeOf(u))) {
            s.serial = 0;
            continue;
        }
        if (s.serial != serial) s = {serial, -1, -1, 0};
        const int x = Field<int16_t>(u, kOffX), y = Field<int16_t>(u, kOffY);
        if (!Idle(u)) {
            // Flying where the mod sent it: leave it be. Anything else is an order of the player's: hands off until the
            // unit has been idle for idle_seconds again.
            const bool ours = s.destX >= 0 && OrderOf(u) == kOrderMove && Field<int16_t>(u, kOffOrderX) == s.destX &&
                              Field<int16_t>(u, kOffOrderY) == s.destY;
            if (!ours) {
                if (s.destX >= 0 && config::g.logCasts) logx::Write("scout type %u at %d,%d: player took over", TypeOf(u), x, y);
                s.destX = s.destY = -1;
                s.idleMs = 0;
            }
            continue;
        }
        if (s.destX >= 0) {
            // Idle again. Where the mod sent it: straight on to the next place. Anywhere else: the player stopped it.
            const bool arrived = Cheb(x, y, s.destX, s.destY) <= kArrived;
            s.destX = s.destY = -1;
            s.idleMs = arrived ? idleNeeded : 0;
        } else {
            s.idleMs += passMs;
        }
        if (s.idleMs < idleNeeded) continue;
        int16_t dx = 0, dy = 0;
        uint32_t age = 0;
        const Why why = PickDestination(w, i, x, y, &dx, &dy, &age);
        if (why == kNone) continue;
        IssueOrder(u, dx, dy, nullptr, kRvaMoveHandler);  // dx, dy are clamped to the map
        if (OrderOf(u) != kOrderMove) continue;
        s.destX = dx;
        s.destY = dy;
        ++g_orders;
        if (config::g.logCasts) {
            if (why == kUnexplored) logx::Write("scout type %u at %d,%d -> %d,%d (unexplored)", TypeOf(u), x, y, dx, dy);
            else logx::Write("scout type %u at %d,%d -> %d,%d (fog, unseen %u s)", TypeOf(u), x, y, dx, dy, age / 1000);
        }
    }
}

}  // namespace

void OnTick(const World& w, unsigned elapsedMs) {
    g_clockMs += elapsedMs;
    if (!config::g.scouts.enabled) {
        g_sincePassMs = 0;
        return;
    }
    g_sincePassMs += elapsedMs;
    if (g_sincePassMs < kPassEveryMs) return;
    const unsigned passMs = g_sincePassMs;
    g_sincePassMs = 0;
    Pass(w, passMs);
}

void OnNewMap() {
    memset(g_state, 0, sizeof(g_state));
    for (ScoutState& s : g_state) s.destX = s.destY = -1;
    memset(g_lastSeen, 0, sizeof(g_lastSeen));
    g_clockMs = 0;
    g_sincePassMs = 0;
    g_orders = 0;
}

unsigned OrderCount() { return g_orders; }

}  // namespace scouts
