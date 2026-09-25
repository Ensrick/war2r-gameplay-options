#pragma once
#include "world.h"

// [dodge]: the local player's units step out of a Blizzard or Death and Decay that is falling (whoever cast it) and do
// not walk into one on their own; what they were doing is given back once the area is gone.
// Evidence: docs/research/dodge.md
namespace dodge {

void OnTick(const game::World& w, unsigned elapsedMs);  // every simulation step, single-player only
void OnNewMap();
unsigned AreaCount();     // areas found by the last pass (tests)
unsigned DodgeCount();    // units moved out since the map started
unsigned HoldCount();     // units held at the edge since the map started
unsigned RestoreCount();  // orders given back since the map started

}  // namespace dodge
