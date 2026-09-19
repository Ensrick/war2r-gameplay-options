#pragma once
#include "world.h"

// [trees] regrow: felled forest grows back, never near a building, never where it would close a passage.
// Evidence for every map, table and rule used here: docs/research/tree_regrowth.md
namespace trees {

void OnTick(const game::World& w, unsigned elapsedMs);  // every simulation step, single-player only
void OnNewMap();  // from the new-map hook: every stump timer starts over
void SeedForTests(uint32_t seed);  // TEST ONLY: fixes the generator behind the per-stump wait (normally seeded from the clock)

}  // namespace trees
