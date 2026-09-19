#pragma once
#include "world.h"

// Gameplay tweaks that are not spell casting. Each one has its own switch in gameplay_options.toml.
namespace tweaks {

void OnTick(const game::World& w, unsigned elapsedMs);  // every simulation step, single-player only

}  // namespace tweaks
