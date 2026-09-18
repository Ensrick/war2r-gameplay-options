#pragma once
#include "world.h"

namespace autocast {

// One sweep over the local player's casters. Called by mod::OnTick on the configured interval.
void Pass(const game::World& w);

}  // namespace autocast
