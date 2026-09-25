#include "farms.h"

#include "config.h"
#include "log.h"

using namespace game;

namespace farms {

namespace {

constexpr unsigned kPassEveryMs = 1000;
constexpr unsigned kWhyEveryMs = 60u * 1000u;  // "why no farm" at most once a minute of play
constexpr int kFoodCap = 200;           // every reader of the supply word clamps to this
constexpr uint8_t kFarmHuman = 0x3A;    // pig farm = 0x3B: the peasant's race bit (type 2 / 3) picks the farm
constexpr uint32_t kSiteNone = 0xFFFFFFFF;
constexpr int kSearchRadius = 16;       // tiles around the hall's footprint
constexpr int kSiteStep = 2;            // the computer's farm rings step 2 tiles (FUN_004db6d0 -> FUN_004dbce0(.., 2))
constexpr int kCorridorMineRange = 12;  // mines this close to the hall have a walking band to it
constexpr int kCorridorClearance = 2;   // free tiles kept between a farm and that band
constexpr int kMaxMines = 16, kMaxBuildings = 256;

unsigned g_sincePassMs = 0;
unsigned g_playMs = 0;
unsigned g_lastWhyMs = 0;
bool g_whyLogged = false;

using CanPlaceFn = uint16_t(__cdecl*)(Unit*, uint32_t, uint32_t);
using TargetTileFn = int(__cdecl*)(Unit*, int16_t*, uint32_t);

struct Size {
    uint16_t w, h;
};

// A footprint, corners inclusive.
struct Rect {
    int x0, y0, x1, y1;
};

Rect RectOf(Unit* u) {
    const Size s = At<Size>(kRvaUnitSizeByType)[TypeOf(u)];
    const int x = Field<int16_t>(u, kOffX), y = Field<int16_t>(u, kOffY);
    return {x, y, x + (s.w ? s.w : 1) - 1, y + (s.h ? s.h : 1) - 1};
}

// Tiles from one footprint to the other: 1 = touching (edges or corners), 0 = overlapping.
int Gap(const Rect& a, const Rect& b) {
    int gx = b.x0 - a.x1, gy = b.y0 - a.y1;
    if (a.x0 - b.x1 > gx) gx = a.x0 - b.x1;
    if (a.y0 - b.y1 > gy) gy = a.y0 - b.y1;
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;
    return gx > gy ? gx : gy;
}

// The band the peasants walk between a hall and a mine: the convex hull of both footprints, in tile-edge
// coordinates (a footprint covers [x0, x1 + 1] x [y0, y1 + 1]).
struct Hull {
    double x[8], y[8];
    int n;
};

double Cross(double ox, double oy, double ax, double ay, double bx, double by) {
    return (ax - ox) * (by - oy) - (ay - oy) * (bx - ox);
}

Hull HullOf(const Rect& a, const Rect& b) {
    double px[8] = {double(a.x0), double(a.x1 + 1), double(a.x0), double(a.x1 + 1),
                    double(b.x0), double(b.x1 + 1), double(b.x0), double(b.x1 + 1)};
    double py[8] = {double(a.y0), double(a.y0), double(a.y1 + 1), double(a.y1 + 1),
                    double(b.y0), double(b.y0), double(b.y1 + 1), double(b.y1 + 1)};
    int idx[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 1; i < 8; ++i)  // sort by x, then y (monotone chain)
        for (int j = i; j > 0 && (px[idx[j]] < px[idx[j - 1]] ||
                                  (px[idx[j]] == px[idx[j - 1]] && py[idx[j]] < py[idx[j - 1]])); --j) {
            const int t = idx[j];
            idx[j] = idx[j - 1];
            idx[j - 1] = t;
        }
    int h[17], k = 0;
    for (int i = 0; i < 8; ++i) {
        while (k >= 2 && Cross(px[h[k - 2]], py[h[k - 2]], px[h[k - 1]], py[h[k - 1]], px[idx[i]], py[idx[i]]) <= 0) --k;
        h[k++] = idx[i];
    }
    for (int i = 6, lower = k + 1; i >= 0; --i) {
        while (k >= lower && Cross(px[h[k - 2]], py[h[k - 2]], px[h[k - 1]], py[h[k - 1]], px[idx[i]], py[idx[i]]) <= 0) --k;
        h[k++] = idx[i];
    }
    Hull hull{};
    hull.n = k - 1;  // the last point repeats the first
    for (int i = 0; i < hull.n; ++i) {
        hull.x[i] = px[h[i]];
        hull.y[i] = py[h[i]];
    }
    return hull;
}

// Is the tile's centre inside the hull (edges included)? The hull runs counter-clockwise in these coordinates.
bool TileInHull(const Hull& hull, int tx, int ty) {
    const double cx = tx + 0.5, cy = ty + 0.5;
    for (int i = 0; i < hull.n; ++i) {
        const int j = (i + 1) % hull.n;
        if (Cross(hull.x[i], hull.y[i], hull.x[j], hull.y[j], cx, cy) < 0) return false;
    }
    return true;
}

bool IsFarm(uint8_t type) { return (type & ~1u) == kFarmHuman; }

// A farm being built, or a worker already on its way to build one: at most one at a time.
bool FarmUnderway(const World& w, uint8_t me) {
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (OwnerOf(u) != me || (Field<uint16_t>(u, kOffStateFlags) & 0x07)) continue;
        if (IsFarm(TypeOf(u)) && !(Field<uint16_t>(u, kOffStateFlags) & kStateComplete)) return true;
        if ((w.typeFlags[TypeOf(u)] & kTfWorker) && OrderOf(u) == kOrderBuild && IsFarm(Field<uint8_t>(u, kOffBuildType)))
            return true;
    }
    return false;
}

// What the farm button tests (FUN_004e4330): the build mask bit of the player and the map's farm bit.
bool FarmAllowed(uint8_t me) {
    return (*At<uint8_t>(kRvaBuildPlayerMask) & (1u << me)) && (At<uint32_t>(kRvaUnitsAllowed)[me] & kAllowFarm);
}

// The live price (the tables a [costs] edit changes), paid by the build action when construction starts.
bool CanPay(uint8_t me, uint8_t type) {
    return At<int32_t>(kRvaPlayerGold)[me] >= At<uint8_t>(kRvaGoldCostByType)[type] * 10 &&
           At<int32_t>(kRvaPlayerLumber)[me] >= At<uint8_t>(kRvaLumberCostByType)[type] * 10 &&
           At<int32_t>(kRvaPlayerOil)[me] >= At<uint8_t>(kRvaOilCostByType)[type] * 10;
}

// Idle = STOP with nothing queued (as in src/workers.cpp); Stand Ground is the player saying "stay".
bool Idle(Unit* u) { return Field<uint8_t>(u, kOffOrder) == kOrderStop && Field<uint8_t>(u, kOffNextOrder) == kOrderNone; }

// A worker we may take: 0 idle, 1 harvesting (which harvesters depends on [farms] workers), -1 never. Only ours,
// outside every building, carrying nothing. Repairing, building, moving, fighting: the player's business.
int Tier(const World& w, Unit* u, uint8_t me) {
    if (OwnerOf(u) != me || !IsActive(u) || !(w.typeFlags[TypeOf(u)] & kTfWorker)) return -1;
    const uint8_t job = Field<uint8_t>(u, kOffWorkerFlags);
    if (job & kWorkerCarrying) return -1;
    if (Idle(u)) return 0;
    if (OrderOf(u) != kOrderHarvest) return -1;
    switch (config::g.farmsWorkers) {
    case FarmWorkers::IdleOnly: return -1;
    case FarmWorkers::Any: return 1;
    default:  // IdleThenLumber: a wood cutter on its way back to the trees, never a gold miner
        return (job & kWorkerLumberJob) && !(job & (kWorkerGoldJob | kWorkerChopping)) ? 1 : -1;
    }
}

uint16_t RegionAt(const World& w, const uint16_t* region, int x, int y) { return region[y * w.mapSize + x]; }
uint16_t RegionAt(const World& w, const uint16_t* region, Unit* u) {
    return RegionAt(w, region, Field<int16_t>(u, kOffX), Field<int16_t>(u, kOffY));
}

int DistanceTo(Unit* u, int x, int y) {
    const int dx = abs(Field<int16_t>(u, kOffX) - x), dy = abs(Field<int16_t>(u, kOffY) - y);
    return dx > dy ? dx : dy;
}

// The worker's nearest complete town hall in its own region, as FUN_004dbc50 picks the centre of the computer's search.
Unit* NearestHall(const World& w, const uint16_t* region, Unit* worker, uint8_t me) {
    const uint16_t r = RegionAt(w, region, worker);
    Unit* best = nullptr;
    int bestDistance = 0;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (OwnerOf(u) != me || (Field<uint16_t>(u, kOffStateFlags) & 0x07) || !(w.typeFlags[TypeOf(u)] & kTfTownHall))
            continue;
        if (!(Field<uint16_t>(u, kOffStateFlags) & kStateComplete) || RegionAt(w, region, u) != r) continue;
        const int d = DistanceTo(worker, Field<int16_t>(u, kOffX), Field<int16_t>(u, kOffY));
        if (!best || d < bestDistance) {
            best = u;
            bestDistance = d;
        }
    }
    return best;
}

enum class Why { None, NoHall, NoSite };

// Rings of step 2 around the hall, as the computer searches, each site checked with the player's own placement test
// FUN_004dc210, but never within mine_clearance of a gold mine or within kCorridorClearance of the band between the
// hall and a mine it serves. Preferred: touching another building on the side away from the mines, then nearest.
uint32_t ChooseSite(const World& w, const uint16_t* region, Unit* worker, Unit* hall, uint8_t farm) {
    const Size fs = At<Size>(kRvaUnitSizeByType)[farm];
    const int fw = fs.w ? fs.w : 2, fh = fs.h ? fs.h : 2;
    const Rect h = RectOf(hall);
    const uint16_t r = RegionAt(w, region, worker);
    const uint8_t me = OwnerOf(worker);

    Rect mines[kMaxMines];
    Hull bands[kMaxMines];
    int mineCount = 0, bandCount = 0;
    Rect buildings[kMaxBuildings];
    int buildingCount = 0;
    double awayX = 0, awayY = 0;  // points from the hall towards its mines; a good site lies against it
    const double hcx = (h.x0 + h.x1 + 1) / 2.0, hcy = (h.y0 + h.y1 + 1) / 2.0;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (Field<uint16_t>(u, kOffStateFlags) & 0x07) continue;
        const Rect ur = RectOf(u);
        if (TypeOf(u) == kTypeGoldMine && mineCount < kMaxMines &&
            Gap(ur, h) <= kSearchRadius + config::g.farmsMineClearance + fw) {
            mines[mineCount++] = ur;
            if (Gap(ur, h) <= kCorridorMineRange) {
                bands[bandCount++] = HullOf(h, ur);
                awayX += (ur.x0 + ur.x1 + 1) / 2.0 - hcx;
                awayY += (ur.y0 + ur.y1 + 1) / 2.0 - hcy;
            }
        } else if (OwnerOf(u) == me && (w.typeFlags[TypeOf(u)] & kTfBuilding) && buildingCount < kMaxBuildings &&
                   Gap(ur, h) <= kSearchRadius + fw) {
            buildings[buildingCount++] = ur;
        }
    }

    uint32_t best = kSiteNone;
    int bestRank = 0, bestGap = 0;
    for (int y = h.y0 - kSearchRadius; y <= h.y1 + kSearchRadius; y += kSiteStep) {
        for (int x = h.x0 - kSearchRadius; x <= h.x1 + kSearchRadius; x += kSiteStep) {
            if (x < 0 || y < 0 || x + fw > w.mapSize || y + fh > w.mapSize) continue;
            if (RegionAt(w, region, x, y) != r) continue;
            const Rect f = {x, y, x + fw - 1, y + fh - 1};
            const int hallGap = Gap(f, h);
            if (hallGap == 0) continue;
            bool rejected = false;
            for (int m = 0; m < mineCount && !rejected; ++m) rejected = Gap(f, mines[m]) <= config::g.farmsMineClearance;
            // The hall's own tiles are part of every band's hull but nobody walks through them: a farm against the
            // hall's far side is fine. (The mine's tiles are covered by mine_clearance.)
            for (int b = 0; b < bandCount && !rejected; ++b)
                for (int ty = f.y0 - kCorridorClearance; ty <= f.y1 + kCorridorClearance && !rejected; ++ty)
                    for (int tx = f.x0 - kCorridorClearance; tx <= f.x1 + kCorridorClearance && !rejected; ++tx)
                        rejected = !(tx >= h.x0 && tx <= h.x1 && ty >= h.y0 && ty <= h.y1) && TileInHull(bands[b], tx, ty);
            if (rejected) continue;

            const double dot = ((f.x0 + f.x1 + 1) / 2.0 - hcx) * awayX + ((f.y0 + f.y1 + 1) / 2.0 - hcy) * awayY;
            const bool away = dot <= 0;
            bool touches = false;
            for (int b = 0; b < buildingCount && !touches; ++b) touches = Gap(f, buildings[b]) == 1;
            const int rank = away && touches ? 0 : (away ? 1 : (touches ? 2 : 3));
            if (best != kSiteNone && (rank > bestRank || (rank == bestRank && hallGap >= bestGap))) continue;

            const uint32_t packed = static_cast<uint16_t>(x) | static_cast<uint32_t>(static_cast<uint16_t>(y)) << 16;
            if (reinterpret_cast<CanPlaceFn>(g_base + kRvaCanPlaceBuilding)(worker, packed, farm) != 0) continue;
            best = packed;
            bestRank = rank;
            bestGap = hallGap;
        }
    }
    return best;
}

void LogWhy(Why why) {
    if (g_whyLogged && g_playMs - g_lastWhyMs < kWhyEveryMs) return;
    g_whyLogged = true;
    g_lastWhyMs = g_playMs;
    logx::Write(why == Why::NoHall ? "farm: food is low but no peasant that may build is near a finished town hall"
                                   : "farm: food is low but there is no free site clear of the gold mines and the paths to them");
}

void Pass(const World& w) {
    const uint8_t me = w.localPlayer;
    const int supply = At<uint16_t>(kRvaFoodSupply)[me];
    const int used = At<uint16_t>(kRvaUnitsCounted)[me] - At<uint16_t>(kRvaFoodFreeUnits)[me];
    const int inTraining = At<uint16_t>(kRvaUnitsInTraining)[me];
    const auto& c = config::g;
    if (!ShouldBuild(supply, used, inTraining, c.farmsFreeMin, c.farmsFreePercent)) return;
    if (!FarmAllowed(me) || FarmUnderway(w, me)) return;

    // The seed decides the race, the region and the hall the search centres on: idle workers first, then the
    // harvesters [farms] workers allows, one search per region (a worker on an island must not block the others).
    const uint16_t* region = *At<uint16_t*>(kRvaRegionMap);
    constexpr int kMaxRegionsTried = 8;
    uint16_t tried[kMaxRegionsTried];
    int triedCount = 0;
    Unit* seed = nullptr;
    uint8_t farm = kFarmHuman;
    uint32_t site = kSiteNone;
    Why why = Why::None;
    for (int tier = 0; tier <= 1 && site == kSiteNone; ++tier) {
        for (unsigned i = 0; i < w.unitCount && site == kSiteNone && triedCount < kMaxRegionsTried; ++i) {
            Unit* u = UnitAt(w, i);
            if (Tier(w, u, me) != tier) continue;
            const uint16_t r = RegionAt(w, region, u);
            bool seen = false;
            for (int k = 0; k < triedCount; ++k) seen = seen || tried[k] == r;
            if (seen) continue;
            tried[triedCount++] = r;
            farm = static_cast<uint8_t>(kFarmHuman | (TypeOf(u) & 1));
            if (!CanPay(me, farm)) return;
            Unit* hall = NearestHall(w, region, u, me);
            if (!hall) {
                if (why == Why::None) why = Why::NoHall;
                continue;
            }
            site = ChooseSite(w, region, u, hall, farm);
            if (site == kSiteNone) why = Why::NoSite;
            seed = u;
        }
    }
    if (site == kSiteNone) {
        if (why != Why::None) LogWhy(why);
        return;
    }
    const int sx = static_cast<int16_t>(site & 0xFFFF), sy = static_cast<int16_t>(site >> 16);

    // The builder: the nearest idle worker in the seed's region, else the nearest harvester [farms] workers allows.
    const uint16_t seedRegion = RegionAt(w, region, seed);
    Unit* builder = nullptr;
    int bestTier = 2, bestDistance = 0;
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        const int t = Tier(w, u, me);
        if (t < 0 || t > bestTier || (TypeOf(u) & 1) != (farm & 1) || RegionAt(w, region, u) != seedRegion) continue;
        const int d = DistanceTo(u, sx, sy);
        if (t < bestTier || d < bestDistance) {
            builder = u;
            bestTier = t;
            bestDistance = d;
        }
    }
    if (!builder) return;
    if (builder != seed &&
        reinterpret_cast<CanPlaceFn>(g_base + kRvaCanPlaceBuilding)(builder, site, farm) != 0)
        return;

    // Exactly what the player's own build click does (FUN_004dc2c0), minus the acknowledgement sound.
    int16_t walkTo[2] = {static_cast<int16_t>(sx), static_cast<int16_t>(sy)};
    reinterpret_cast<TargetTileFn>(g_base + kRvaBuildTargetTile)(builder, walkTo, farm);
    Field<uint8_t>(builder, kOffBuildType) = farm;
    Field<uint32_t>(builder, kOffBuildSite) = site;
    const int bx = Field<int16_t>(builder, kOffX), by = Field<int16_t>(builder, kOffY);
    IssueOrder(builder, walkTo[0], walkTo[1], nullptr, kRvaBuildHandler);
    if (OrderOf(builder) != kOrderBuild) return;  // refused (off the map): nothing was started
    if (c.logCasts)
        logx::Write("farm: peasant at %d,%d -> farm at %d,%d (food %d/%d)", bx, by, sx, sy, used + inTraining,
                    supply > kFoodCap ? kFoodCap : supply);
}

}  // namespace

bool ShouldBuild(int supply, int used, int inTraining, int freeMin, int freePercent) {
    if (supply >= kFoodCap) return false;  // a farm would add nothing
    const int percentFree = (freePercent * supply + 99) / 100;
    const int threshold = freeMin > percentFree ? freeMin : percentFree;  // the HIGHER, as auto-production keeps free
    return supply - used - inTraining <= threshold;
}

void ResetForTests() {
    g_sincePassMs = 0;
    g_playMs = 0;
    g_lastWhyMs = 0;
    g_whyLogged = false;
}

void OnTick(const World& w, unsigned elapsedMs) {
    if (!config::g.farmsAutoBuild) return;
    g_playMs += elapsedMs;
    g_sincePassMs += elapsedMs;
    if (g_sincePassMs < kPassEveryMs) return;
    g_sincePassMs = 0;
    Pass(w);
}

}  // namespace farms
