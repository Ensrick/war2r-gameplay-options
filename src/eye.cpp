#include "eye.h"

#include "config.h"
#include "log.h"

using namespace game;

namespace eye {

namespace {

// The computer's own eye only random-walks (AI order 4, FUN_004cc400). We steer by the local player's explored map.
struct Steered {
    uint32_t serial;  // unit creation serial (+0x14); 0 = free slot
    int16_t x, y;     // last destination we issued
    bool released;    // the player gave this eye an order of their own: hands off from then on
    bool seen;
};
constexpr int kMaxSteered = 16;
Steered g_steered[kMaxSteered];

uint32_t g_rng = 0x2545F491;
uint32_t Rand() {  // xorshift32; the game's own RNG is left alone so the mod never perturbs the simulation's dice
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool IsEyeCaster(uint8_t type) { return type == kTypeOgreMage || type == kTypeOgreMageHero || type == kTypeChogall; }

// How much of the eye's sight window (sight 3 -> 7x7) around a tile matches `value` in a tile map.
int CountInWindow(const uint8_t* map, int size, int cx, int cy, uint8_t value) {
    int n = 0;
    for (int y = cy - 3; y <= cy + 3; ++y) {
        if (y < 0 || y >= size) continue;
        for (int x = cx - 3; x <= cx + 3; ++x)
            if (x >= 0 && x < size && map[y * size + x] == value) ++n;
    }
    return n;
}

// Best of a handful of random tiles: most new ground for the least flying. False when the map has nothing left.
bool PickDestination(const uint8_t* map, int size, int ex, int ey, int16_t* outX, int16_t* outY) {
    int bestScore = 0;
    for (int k = 0; k < 48; ++k) {
        // First tries stay within ~20 tiles, later ones may land anywhere on the map.
        const int reach = k < 32 ? 20 : size;
        const int x = Clamp(ex - reach + static_cast<int>(Rand() % (2 * reach + 1)), 0, size - 1);
        const int y = Clamp(ey - reach + static_cast<int>(Rand() % (2 * reach + 1)), 0, size - 1);
        const int fresh = CountInWindow(map, size, x, y, kTileUnexplored);
        if (!fresh) continue;
        const int dx = x > ex ? x - ex : ex - x, dy = y > ey ? y - ey : ey - y;
        const int score = fresh * 4 - (dx > dy ? dx : dy) + 200;
        if (score > bestScore) {
            bestScore = score;
            *outX = static_cast<int16_t>(x);
            *outY = static_cast<int16_t>(y);
        }
    }
    return bestScore > 0;
}

Steered* FindOrAdopt(uint32_t serial) {
    Steered* free = nullptr;
    for (Steered& s : g_steered) {
        if (s.serial == serial) return &s;
        if (!s.serial && !free) free = &s;
    }
    if (free) *free = {serial, -1, -1, false, true};
    return free;
}

void Scout(const World& w, Unit* e) {
    Steered* s = FindOrAdopt(Field<uint32_t>(e, kOffSerial));
    if (!s) return;
    s->seen = true;
    if (s->released || OrderOf(e) != kOrderStop) return;  // still flying, or doing something the player asked for
    const int ex = Field<int16_t>(e, kOffX), ey = Field<int16_t>(e, kOffY);
    if (s->x >= 0) {
        const int dx = ex > s->x ? ex - s->x : s->x - ex, dy = ey > s->y ? ey - s->y : s->y - ey;
        if (dx > 2 || dy > 2) {
            s->released = true;  // it came to rest somewhere we never sent it: the player is flying this one
            return;
        }
    }

    const uint8_t* explored = *At<uint8_t*>(kRvaExploredMap);
    const uint8_t* visible = *At<uint8_t*>(kRvaVisibleMap);
    int16_t x = 0, y = 0;
    const bool found = (explored && PickDestination(explored, w.mapSize, ex, ey, &x, &y)) ||
                       (visible && PickDestination(visible, w.mapSize, ex, ey, &x, &y));  // all explored: go look at fog
    if (!found) {  // nothing hidden anywhere (map revealed): wander like the computer's eye does
        const int reach = w.mapSize / 4 + 1;
        x = static_cast<int16_t>(Clamp(ex - reach + static_cast<int>(Rand() % (2 * reach + 1)), 0, w.mapSize - 1));
        y = static_cast<int16_t>(Clamp(ey - reach + static_cast<int>(Rand() % (2 * reach + 1)), 0, w.mapSize - 1));
    }
    if (x == ex && y == ey) return;
    IssueOrder(e, x, y, nullptr, kRvaMoveHandler);  // the handler indexes the unit grid with x,y: both are clamped above
    if (OrderOf(e) != kOrderMove) return;
    s->x = x;
    s->y = y;
    if (config::g.logCasts) logx::Write("eye %u at %d,%d -> scouting %d,%d", s->serial, ex, ey, x, y);
}

}  // namespace

void Pass(const World& w) {
    const bool cast = config::g.eyeCast, scout = config::g.eyeAutoScout;
    if (!cast && !scout) return;

    for (Steered& s : g_steered) s.seen = false;
    int eyesAlive = 0;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (OwnerOf(u) != w.localPlayer || !IsActive(u)) continue;
        if (TypeOf(u) == kTypeEye) {
            ++eyesAlive;
            if (scout) Scout(w, u);
        } else if (IsEyeCaster(TypeOf(u)) && OrderOf(u) == kOrderSpellEye) {
            ++eyesAlive;  // a cast that has not produced its eye yet
        }
    }
    for (Steered& s : g_steered)
        if (!s.seen) s.serial = 0;  // that eye is gone

    if (!cast || eyesAlive >= config::g.eyeMaxActive) return;
    if (!(At<uint32_t>(kRvaSpellsResearched)[w.localPlayer] & 0x400)) return;
    const int cost = At<uint16_t>(kRvaManaCostByOrder)[kOrderSpellEye];
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (OwnerOf(u) != w.localPlayer || !IsActive(u) || !IsEyeCaster(TypeOf(u))) continue;
        const int mana = Field<uint8_t>(u, kOffMana);
        if (mana < cost || mana < config::g.eyeCastAtMana) continue;
        if (Field<uint16_t>(u, kOffInvisTimer) != 0) continue;
        const uint8_t order = OrderOf(u);
        if (order != kOrderStop && order != kOrderStand) continue;  // the cast ends in "stop": only take idle casters
        IssueSpell(u, kOrderSpellEye, Field<int16_t>(u, kOffX), Field<int16_t>(u, kOffY), nullptr);
        if (OrderOf(u) != kOrderSpellEye) continue;
        if (config::g.logCasts) logx::Write("cast eye_of_kilrogg: caster at %d,%d", Field<int16_t>(u, kOffX), Field<int16_t>(u, kOffY));
        if (++eyesAlive >= config::g.eyeMaxActive) return;
    }
}

}  // namespace eye
