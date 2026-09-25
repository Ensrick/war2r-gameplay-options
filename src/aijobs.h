#pragma once
#include "world.h"

// [general] fix_ai_after_load: repairs a bug in the game. Loading a savegame zeroes the computer players'
// worker-job counters but keeps every worker's job bits, so the first worker to deliver wraps its counter to 65535
// and the computer never sends a worker to the trees (or to repair) again. This file puts each counter back to the
// number of units that carry its bit, which is the value the game itself keeps outside that bug.
// Plain data writes only: no order, no hook. Single player only (the caller is behind the multiplayer gate).
// Evidence: docs/research/ai_lumber.md.
namespace aijobs {

void OnTick(const game::World& w);  // every simulation step, single player only

// The three counters of one player, for the log_ai status line.
struct Jobs {
    uint16_t gold, lumber, repair;
};
Jobs Read(int player);

// TEST ONLY.
using Sink = void (*)(const char* line);
void SetSinkForTests(Sink sink);  // nullptr restores the normal log file

}  // namespace aijobs
