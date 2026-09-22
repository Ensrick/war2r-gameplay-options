#pragma once
#include "world.h"

// [autocast] resume_orders: every order the mod issues clears the Remastered resume byte (+0x8D), which is what
// keeps a paladin from crashing the game mid-heal (docs/DEV_HISTORY.md 1.16.2) but also loses the attack-move or
// patrol the unit was on. This remembers what was there and gives it back once the unit is idle again.
namespace resume {

// Called from game::IssueOrder just before the resume byte is cleared. Does nothing when the unit has none.
void Remember(game::Unit* unit);
// One pass per tick: hands back the remembered order to every caster that has gone idle, and forgets the rest.
void Tick(const game::World& w);
void OnNewMap();

unsigned Count();        // records being kept (tests)
unsigned RestoreCount();  // orders handed back since load (tests)

}  // namespace resume
