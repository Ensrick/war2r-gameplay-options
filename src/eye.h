#pragma once
#include "world.h"

// Eye of Kilrogg: ogre-magi cast it on their own and the eye flies to unexplored ground.
namespace eye {

void Pass(const game::World& w);  // on the autocast interval, under the same master switch

}  // namespace eye
