#include "farms.h"

#include "config.h"
#include "log.h"

using namespace game;

namespace farms {

namespace {

constexpr unsigned kPassEveryMs = 1000;
constexpr int kFoodCap = 200;           // every reader of the supply word clamps to this
constexpr uint8_t kFarmHuman = 0x3A;    // pig farm = 0x3B: the peasant's race bit (type 2 / 3) picks the farm
constexpr uint32_t kSiteNone = 0xFFFFFFFF;

unsigned g_sincePassMs = 0;

using FindSiteFn = int(__cdecl*)(Unit*, int16_t*, uint32_t);
using CanPlaceFn = uint16_t(__cdecl*)(Unit*, uint32_t, uint32_t);
using TargetTileFn = int(__cdecl*)(Unit*, int16_t*, uint32_t);

struct Size {
    uint16_t w, h;
};

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

// A worker we may take: ours, outside every building, carrying nothing, and idle (tier 0) or harvesting (tier 1).
// Anything else (repairing, building, moving, fighting) is the player's business. -1 = not a candidate.
int Tier(const World& w, Unit* u, uint8_t me) {
    if (OwnerOf(u) != me || !IsActive(u) || !(w.typeFlags[TypeOf(u)] & kTfWorker)) return -1;
    if (Field<uint8_t>(u, kOffWorkerFlags) & kWorkerCarrying) return -1;
    if (Idle(u)) return 0;
    if (OrderOf(u) == kOrderHarvest) return 1;
    return -1;
}

uint16_t RegionAt(const World& w, const uint16_t* region, Unit* u) {
    return region[Field<int16_t>(u, kOffY) * w.mapSize + Field<int16_t>(u, kOffX)];
}

int DistanceTo(Unit* u, int x, int y) {
    const int dx = abs(Field<int16_t>(u, kOffX) - x), dy = abs(Field<int16_t>(u, kOffY) - y);
    return dx > dy ? dx : dy;
}

// The computer's own search, then the player's own placement test. kSiteNone when there is no free site.
uint32_t FindSite(const World& w, Unit* worker, uint8_t type) {
    int16_t site[2] = {-1, -1};
    if (!reinterpret_cast<FindSiteFn>(g_base + kRvaAiFindBuildSite)(worker, site, type)) return kSiteNone;
    const Size size = At<Size>(kRvaUnitSizeByType)[type];
    if (site[0] < 0 || site[1] < 0 || site[0] + size.w > w.mapSize || site[1] + size.h > w.mapSize) return kSiteNone;
    const uint32_t packed = static_cast<uint16_t>(site[0]) | static_cast<uint32_t>(static_cast<uint16_t>(site[1])) << 16;
    if (reinterpret_cast<CanPlaceFn>(g_base + kRvaCanPlaceBuilding)(worker, packed, type) != 0) return kSiteNone;
    return packed;
}

void Pass(const World& w) {
    const uint8_t me = w.localPlayer;
    const int supply = At<uint16_t>(kRvaFoodSupply)[me];
    const int used = At<uint16_t>(kRvaUnitsCounted)[me] - At<uint16_t>(kRvaFoodFreeUnits)[me];
    const int inTraining = At<uint16_t>(kRvaUnitsInTraining)[me];
    const auto& c = config::g;
    if (!ShouldBuild(supply, used, inTraining, c.farmsFreeMin, c.farmsFreePercent)) return;
    if (!FarmAllowed(me) || FarmUnderway(w, me)) return;

    // The seed decides the race, the region and (through its nearest hall) where the computer's search looks: idle
    // workers first, then harvesters, one search per region (a worker on an island must not block the others).
    const uint16_t* region = *At<uint16_t*>(kRvaRegionMap);
    constexpr int kMaxRegionsTried = 8;
    uint16_t tried[kMaxRegionsTried];
    int triedCount = 0;
    Unit* seed = nullptr;
    uint8_t farm = kFarmHuman;
    uint32_t site = kSiteNone;
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
            site = FindSite(w, u, farm);
            seed = u;
        }
    }
    if (site == kSiteNone) return;
    const int sx = static_cast<int16_t>(site & 0xFFFF), sy = static_cast<int16_t>(site >> 16);

    // The builder: the nearest idle worker in the seed's region, else the nearest harvester carrying nothing.
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

void ResetForTests() { g_sincePassMs = 0; }

void OnTick(const World& w, unsigned elapsedMs) {
    if (!config::g.farmsAutoBuild) return;
    g_sincePassMs += elapsedMs;
    if (g_sincePassMs < kPassEveryMs) return;
    g_sincePassMs = 0;
    Pass(w);
}

}  // namespace farms
