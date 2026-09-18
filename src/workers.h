#pragma once
#include "world.h"

// Idle peasants / peons of the local player: repair nearby damage, otherwise go back to gold or lumber.
// Evidence for every order shape used here: docs/research/workers_and_gold.md
namespace workers {

void OnTick(const game::World& w, unsigned elapsedMs);  // every simulation step, single-player only

}  // namespace workers
