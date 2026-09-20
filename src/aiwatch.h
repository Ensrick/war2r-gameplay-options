#pragma once
#include "world.h"

// [general] log_ai: a read-only report on the computer players' script interpreter, for diagnosing a computer that
// stops attacking. It writes one line per computer player per minute of play and a separate line when a player has
// been stuck on the same instruction for five minutes.
//
// This file NEVER writes to game memory and never issues an order: the mod does not drive the computer, it only says
// what the computer is doing. Every address and the whole decoding come from docs/research/ai_stall.md.
namespace aiwatch {

void OnTick(const game::World& w, unsigned elapsedMs);  // every simulation step, single player only
void OnNewMap();  // from the new-map hook: forget every player's history

// TEST ONLY.
using Sink = void (*)(const char* line);
void SetSinkForTests(Sink sink);  // nullptr restores the normal log file
void ResetForTests();             // forget the history and the report timer

}  // namespace aiwatch
