#include "mineworkers.h"

#include "config.h"

using namespace game;

namespace mineworkers {

namespace {

struct Size {
    uint16_t w, h;
};

struct Box {
    int x0, y0, x1, y1;
};

Box BoxOf(Unit* u) {
    const Size size = At<Size>(kRvaUnitSizeByType)[TypeOf(u)];
    const int x0 = Field<int16_t>(u, kOffX), y0 = Field<int16_t>(u, kOffY);
    return {x0, y0, x0 + (size.w ? size.w - 1 : 0), y0 + (size.h ? size.h - 1 : 0)};
}

// Town hall, keep, castle and the orc ones: the six depots (docs/research/workers_and_gold.md, type flag 0x1000).
bool IsHall(uint8_t type) {
    return type == kTypeTownHall || type == kTypeTownHall + 1 || type == kTypeKeep || type == kTypeKeep + 1 ||
           type == kTypeCastle || type == kTypeCastle + 1;
}

bool IsWorker(const World& w, Unit* u) { return (w.typeFlags[TypeOf(u)] & kTfWorker) != 0; }

// A tile on or next to the mine in region r: the mine's own tiles are a building, the ring around it is where miners
// stand. The same test the farm feature uses for halls would do, but only the ring is certain to be walkable land.
bool MineInRegion(const World& w, const uint16_t* region, Unit* mine, uint16_t r) {
    if (!region) return true;
    const Box b = BoxOf(mine);
    for (int y = b.y0 - 1; y <= b.y1 + 1; ++y)
        for (int x = b.x0 - 1; x <= b.x1 + 1; ++x)
            if (x >= 0 && y >= 0 && x < w.mapSize && y < w.mapSize && region[y * w.mapSize + x] == r) return true;
    return false;
}

}  // namespace

int FootprintGap(Unit* a, Unit* b) {
    const Box p = BoxOf(a), q = BoxOf(b);
    const int dx = q.x0 > p.x1 ? q.x0 - p.x1 - 1 : (p.x0 > q.x1 ? p.x0 - q.x1 - 1 : 0);
    const int dy = q.y0 > p.y1 ? q.y0 - p.y1 - 1 : (p.y0 > q.y1 ? p.y0 - q.y1 - 1 : 0);
    return dx > dy ? dx : dy;
}

int DistanceToFootprint(int px, int py, Unit* u) {
    const Box b = BoxOf(u);
    const int dx = px < b.x0 ? b.x0 - px : (px > b.x1 ? px - b.x1 : 0);
    const int dy = py < b.y0 ? b.y0 - py : (py > b.y1 ? py - b.y1 : 0);
    return dx > dy ? dx : dy;
}

uint16_t RegionOf(const World& w, Unit* u) {
    const uint16_t* region = *At<uint16_t*>(kRvaRegionMap);
    const int x = Field<int16_t>(u, kOffX), y = Field<int16_t>(u, kOffY);
    if (!region || x < 0 || y < 0 || x >= w.mapSize || y >= w.mapSize) return 0;
    return region[y * w.mapSize + x];
}

int Want(int hundreds) {
    if (hundreds <= 0) return 0;
    const int gold = hundreds * 100;
    const auto& c = config::g;
    if (gold >= c.mineWorkersRichGold) return c.mineWorkersRich;
    if (gold >= c.mineWorkersMediumGold) return c.mineWorkersMedium;
    return c.mineWorkersPoor;
}

Unit* MineOf(const World& w, Unit* worker) {
    if (OwnerOf(worker) != w.localPlayer || (Field<uint8_t>(worker, kOffStateFlags) & kStateGoneMask) || !IsWorker(w, worker))
        return nullptr;
    const uint8_t order = OrderOf(worker);
    Unit* target = Field<Unit*>(worker, kOffOrderTarget);
    // Walking to the mine, or inside it (the enter order keeps the mine as its target).
    if ((order == kOrderHarvest || order == kOrderEnter) && target && TypeOf(target) == kTypeGoldMine) return target;
    // Carrying its gold home, handing it in inside the hall, stepping out to go back: the mine is saved in +0x70.
    const uint8_t flags = Field<uint8_t>(worker, kOffWorkerFlags);
    if ((order == kOrderReturnGoods || order == kOrderEnter || order == kOrderLeave) && (flags & kWorkerGoldJob) &&
        (flags & kWorkerSavedMine))
        return Field<Unit*>(worker, kOffSavedMine);
    return nullptr;
}

int Collect(const World& w, Mine* out, int max) {
    if (!config::g.mineWorkers) return 0;
    const uint16_t* region = *At<uint16_t*>(kRvaRegionMap);
    const int radius = config::g.mineWorkersRadius;
    int n = 0;
    for (unsigned i = 0; i < w.unitCount && n < max; ++i) {
        Unit* m = UnitAt(w, i);
        if (TypeOf(m) != kTypeGoldMine || !IsActive(m)) continue;
        const int want = Want(Field<uint16_t>(m, kOffResources));
        if (want <= 0) continue;
        Unit* best = nullptr;
        int bestGap = radius + 1;
        for (unsigned j = 0; j < w.unitCount; ++j) {
            Unit* h = UnitAt(w, j);
            if (OwnerOf(h) != w.localPlayer || !IsHall(TypeOf(h)) || !IsActive(h)) continue;
            if (!(Field<uint16_t>(h, kOffStateFlags) & kStateComplete)) continue;
            const int gap = FootprintGap(h, m);
            if (gap >= bestGap || !MineInRegion(w, region, m, RegionOf(w, h))) continue;
            best = h;
            bestGap = gap;
        }
        if (best) out[n++] = {m, best, want, 0};
    }
    if (n == 0) return 0;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* mine = MineOf(w, UnitAt(w, i));
        if (!mine) continue;
        for (int k = 0; k < n; ++k)
            if (out[k].mine == mine) {
                ++out[k].have;
                break;
            }
    }
    return n;
}

int IdleNear(const World& w, Unit* hall) {
    const int radius = config::g.mineWorkersRadius;
    const uint16_t r = RegionOf(w, hall);
    int n = 0;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (OwnerOf(u) != w.localPlayer || !IsActive(u) || !IsWorker(w, u)) continue;
        if (Field<uint8_t>(u, kOffOrder) != kOrderStop || Field<uint8_t>(u, kOffNextOrder) != kOrderNone) continue;
        if (Field<uint8_t>(u, kOffWorkerFlags) & kWorkerCarrying) continue;
        if (DistanceToFootprint(Field<int16_t>(u, kOffX), Field<int16_t>(u, kOffY), hall) > radius) continue;
        if (RegionOf(w, u) != r) continue;
        ++n;
    }
    return n;
}

}  // namespace mineworkers
