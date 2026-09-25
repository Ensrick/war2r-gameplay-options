#pragma once
#include "world.h"

// [farms] auto_build: when your free food runs low, one of your peasants builds a farm, placed by the computer
// player's own site search next to your town hall. Local player only; the caller is behind the multiplayer gate.
// Evidence: docs/research/farms.md.
namespace farms {

void OnTick(const game::World& w, unsigned elapsedMs);

// The trigger: free food (supply up to 200, minus units alive and in training) at or below the HIGHER of free_min
// and free_percent of the supply (rounded up). Never at 200 supply.
bool ShouldBuild(int supply, int used, int inTraining, int freeMin, int freePercent);

void ResetForTests();  // forget the pass timer

}  // namespace farms
