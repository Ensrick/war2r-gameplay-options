#pragma once
#include "world.h"

// [scouts]: the local player's idle flying machines and zeppelins (or whatever `units` lists) fly off to look at
// ground nobody has explored, then keep patrolling the fog that has gone longest unseen. Ctrl + toggle_key turns it
// on and off in game. Evidence: docs/research/scouts.md
namespace scouts {

void OnTick(const game::World& w, unsigned elapsedMs);  // every simulation step, single-player only
void OnNewMap();
unsigned OrderCount();  // move orders issued since the map started (tests, diagnostics)

}  // namespace scouts
