#include "workers.h"

#include "config.h"
#include "log.h"

using namespace game;

namespace workers {

namespace {

// The unit struct has no idle counter, so we keep our own, keyed by unit slot and verified by creation serial.
constexpr unsigned kMaxSlots = 2048;
struct IdleInfo {
    uint32_t serial;
    uint32_t idleMs;  // play time spent idle; pauses are not counted (elapsedMs is 0 across them)
};
IdleInfo g_idle[kMaxSlots];
unsigned g_sincePassMs = 0;
constexpr unsigned kPassEveryMs = 250;

struct Size {
    uint16_t w, h;
};

// Tile distance from a point to a unit's footprint (buildings are 2x2 to 4x4, their x,y is the top-left tile).
int DistanceToFootprint(int px, int py, Unit* u) {
    const Size size = At<Size>(kRvaUnitSizeByType)[TypeOf(u)];
    const int x0 = Field<int16_t>(u, kOffX), y0 = Field<int16_t>(u, kOffY);
    const int x1 = x0 + (size.w ? size.w - 1 : 0), y1 = y0 + (size.h ? size.h - 1 : 0);
    const int dx = px < x0 ? x0 - px : (px > x1 ? px - x1 : 0);
    const int dy = py < y0 ? y0 - py : (py > y1 ? py - y1 : 0);
    return dx > dy ? dx : dy;
}

bool OnMap(const World& w, int x, int y) { return x >= 0 && y >= 0 && x < w.mapSize && y < w.mapSize; }

// A tree the worker can actually reach: a forest tile with a neighbour tile in the worker's own region.
// Same test as the game's tree search predicate (FUN_004eaf60), minus its square-flag check.
// Outside the map there is nothing, not even forest: until 1.4.0 an off-map tile counted as a tree, so a worker idling
// on the map's edge was sent to x or y = -1 and the game read its unit grid out of bounds (0x4D80BF, crash 2026-09-19).
bool TreeReachable(const World& w, const uint16_t* region, int x, int y, uint16_t workerRegion) {
    if (!OnMap(w, x, y) || region[y * w.mapSize + x] != kRegionTree) return false;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if ((dx || dy) && OnMap(w, x + dx, y + dy) && region[(y + dy) * w.mapSize + x + dx] == workerRegion) return true;
    return false;
}

// Orders given here stand in for the player's own clicks, so mirror the command path: it clears the Remastered
// resume-order byte first (FUN_004dcc60), otherwise an old patrol / attack-move could resume over our order.
bool Issue(Unit* worker, int16_t x, int16_t y, Unit* target, uint32_t handlerRva, uint8_t expectOrder) {
    if (*At<uint32_t>(kRvaRuleset) != 0) Field<uint8_t>(worker, kOffResumeOrder) = kOrderNone;
    IssueOrder(worker, x, y, target, handlerRva);
    return OrderOf(worker) == expectOrder;
}

bool TryRepair(const World& w, Unit* worker) {
    const uint8_t me = w.localPlayer;
    // The repair action stops the worker with an on-screen complaint when either resource is at zero.
    if (At<int32_t>(kRvaPlayerGold)[me] <= 0 || At<int32_t>(kRvaPlayerLumber)[me] <= 0) return false;
    const int wx = Field<int16_t>(worker, kOffX), wy = Field<int16_t>(worker, kOffY);
    Unit* best = nullptr;
    int bestDistance = config::g.workerRepairRadius + 1;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* b = UnitAt(w, i);
        if (OwnerOf(b) != me || !IsActive(b) || !(w.typeFlags[TypeOf(b)] & kTfBuilding)) continue;
        // Unfinished sites are excluded: "repairing" one is the power-build exploit and drains resources.
        if (!(Field<uint16_t>(b, kOffStateFlags) & kStateComplete)) continue;
        if (Field<uint16_t>(b, kOffHp) >= MaxHp(w, b)) continue;
        const int d = DistanceToFootprint(wx, wy, b);
        if (d < bestDistance) {
            bestDistance = d;
            best = b;
        }
    }
    if (!best || !Issue(worker, 0, 0, best, kRvaRepairHandler, kOrderRepair)) return false;
    if (config::g.logCasts) logx::Write("worker at %d,%d -> repair building type %u at %d,%d", wx, wy, TypeOf(best),
                                        Field<int16_t>(best, kOffX), Field<int16_t>(best, kOffY));
    return true;
}

bool TryHarvest(const World& w, Unit* worker) {
    const int wx = Field<int16_t>(worker, kOffX), wy = Field<int16_t>(worker, kOffY);
    // A harvest order on a loaded worker would relabel its cargo as lumber: carry it home instead.
    if (Field<uint8_t>(worker, kOffWorkerFlags) & kWorkerCarrying) return Issue(worker, 0, 0, nullptr, kRvaReturnHandler, kOrderReturnGoods);

    const uint16_t* region = *At<uint16_t*>(kRvaRegionMap);
    if (!region || !OnMap(w, wx, wy)) return false;
    const uint16_t workerRegion = region[wy * w.mapSize + wx];
    const int radius = config::g.workerHarvestRadius;

    Unit* mine = nullptr;
    int mineDistance = radius + 1;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* m = UnitAt(w, i);
        if (TypeOf(m) != kTypeGoldMine || !IsActive(m) || Field<uint16_t>(m, kOffResources) == 0) continue;
        const int d = DistanceToFootprint(wx, wy, m);
        if (d < mineDistance) {
            mineDistance = d;
            mine = m;
        }
    }

    int treeX = 0, treeY = 0, treeDistance = radius + 1;
    for (int d = 1; d <= radius && treeDistance > radius; ++d)  // rings outward: the first hit is the nearest
        for (int y = wy - d; y <= wy + d && treeDistance > radius; ++y)
            for (int x = wx - d; x <= wx + d; ++x) {
                if (x != wx - d && x != wx + d && y != wy - d && y != wy + d) continue;  // ring edge only
                if (!TreeReachable(w, region, x, y, workerRegion)) continue;
                treeX = x;
                treeY = y;
                treeDistance = d;
                break;
            }

    if (mine && mineDistance <= treeDistance) {
        if (!Issue(worker, 0, 0, mine, kRvaHarvestHandler, kOrderHarvest)) return false;
        if (config::g.logCasts) logx::Write("worker at %d,%d -> gold mine at %d,%d", wx, wy, Field<int16_t>(mine, kOffX), Field<int16_t>(mine, kOffY));
        return true;
    }
    if (treeDistance <= radius) {
        if (!Issue(worker, static_cast<int16_t>(treeX), static_cast<int16_t>(treeY), nullptr, kRvaHarvestHandler, kOrderHarvest)) return false;
        if (config::g.logCasts) logx::Write("worker at %d,%d -> tree at %d,%d", wx, wy, treeX, treeY);
        return true;
    }
    return false;
}

}  // namespace

void OnTick(const World& w, unsigned elapsedMs) {
    const bool harvest = config::g.workerAutoHarvest, repair = config::g.workerAutoRepair;
    if (!harvest && !repair) return;
    g_sincePassMs += elapsedMs;
    if (g_sincePassMs < kPassEveryMs) return;
    const unsigned stepMs = g_sincePassMs;
    g_sincePassMs = 0;

    const unsigned count = w.unitCount < kMaxSlots ? w.unitCount : kMaxSlots;
    for (unsigned i = 0; i < count; ++i) {
        Unit* u = UnitAt(w, i);
        IdleInfo& info = g_idle[i];
        // Idle = STOP with nothing queued. Stand Ground (13) is the player saying "stay", and is respected.
        const bool idleWorker = OwnerOf(u) == w.localPlayer && IsActive(u) && (w.typeFlags[TypeOf(u)] & kTfWorker) &&
                                Field<uint8_t>(u, kOffOrder) == kOrderStop && Field<uint8_t>(u, kOffNextOrder) == kOrderNone;
        const uint32_t serial = Field<uint32_t>(u, kOffSerial);
        if (!idleWorker || info.serial != serial) {
            info.serial = serial;
            info.idleMs = 0;
            if (!idleWorker) continue;
        }
        info.idleMs += stepMs;

        bool ordered = false;
        if (repair && info.idleMs >= static_cast<uint32_t>(config::g.workerRepairIdleSeconds) * 1000) ordered = TryRepair(w, u);
        if (!ordered && harvest && info.idleMs >= static_cast<uint32_t>(config::g.workerHarvestIdleSeconds) * 1000) {
            ordered = TryHarvest(w, u);
            // Nothing to do here either: start the wait over instead of rescanning the map four times a second.
            if (!ordered) info.idleMs = 0;
        }
        if (ordered) info.idleMs = 0;
    }
}

}  // namespace workers
