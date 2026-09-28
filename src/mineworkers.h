#pragma once
#include "world.h"

// Mine crews: every gold mine near a finished hall of the local player wants a few of his workers mining it, by the gold
// it still holds ([workers] mine_workers*). production.cpp trains the missing ones at that hall, workers.cpp sends idle
// workers there. Evidence: docs/research/mine_workers.md
namespace mineworkers {

constexpr int kMaxMines = 64;

struct Mine {
    game::Unit* mine;
    game::Unit* hall;  // the nearest finished hall of the local player, in the mine's region
    int want;          // workers the gold left asks for
    int have;          // workers of the local player mining it right now (walking there, inside, carrying from it)
};

// Workers the gold left asks for: 0 for an empty mine. `hundreds` is the mine's own field (+0x82, x100 gold).
int Want(int hundreds);

// The mine a worker of the local player is mining, or nullptr: on its way to a mine (harvest order on it), inside it,
// or on the round trip after it (carrying gold with the mine saved: returning, inside the hall, leaving).
game::Unit* MineOf(const game::World& w, game::Unit* worker);

// Every live mine with gold that has a finished hall of the local player within mine_workers_radius tiles (footprint to
// footprint) in its own region, each mine once, counted toward its nearest hall. Empty when [workers] mine_workers is off.
int Collect(const game::World& w, Mine* out, int max);

// Idle workers of the local player (not carrying) within mine_workers_radius tiles of the hall: the ones workers.cpp is
// about to send to its mines, so the hall does not train a worker for a place one of them will fill.
int IdleNear(const game::World& w, game::Unit* hall);

// Tiles between two footprints (0 = touching or overlapping), Chebyshev.
int FootprintGap(game::Unit* a, game::Unit* b);
// Tiles from a point to a footprint.
int DistanceToFootprint(int px, int py, game::Unit* u);
uint16_t RegionOf(const game::World& w, game::Unit* u);  // 0 when the map has no region map

}  // namespace mineworkers
