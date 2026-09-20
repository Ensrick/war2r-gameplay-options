#pragma once
#include "world.h"

namespace autocast {

// One sweep over the local player's casters. Called by mod::OnTick on the configured interval.
void Pass(const game::World& w);

// Only the Blizzard / Death and Decay watchdog of Pass: channels the mod started are still stopped when they turn
// unsafe while autocasting is switched off (Ctrl+F9 or [general] enabled = false).
void GuardChannels(const game::World& w);

// Play time (ms per simulation step, 0 across pauses) for the log throttle of the Raise Dead diagnostic.
void AddPlayTime(unsigned ms);
// With [general] log_casts on, a death knight that could not raise the dead logs why, once per 30 s of play.
unsigned RaiseDeadNoteCount();   // lines written so far (tests, diagnostics)
const char* LastRaiseDeadNote();  // the reason of the last one

// Tests: what the mod has done, so a pass that must do nothing can be proved to have done nothing.
unsigned CastCount();     // spells cast since load
unsigned ChannelCount();  // Blizzard / Death and Decay channels the watchdog is following

}  // namespace autocast
