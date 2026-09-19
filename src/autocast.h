#pragma once
#include "world.h"

namespace autocast {

// One sweep over the local player's casters. Called by mod::OnTick on the configured interval.
void Pass(const game::World& w);

// Only the Blizzard / Death and Decay watchdog of Pass: channels the mod started are still stopped when they turn
// unsafe while autocasting is switched off (Ctrl+F9 or [general] enabled = false).
void GuardChannels(const game::World& w);

}  // namespace autocast
