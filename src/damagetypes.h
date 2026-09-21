#pragma once
#include <cstdint>

#include "config.h"
#include "world.h"

// [weapon_types] / [armor_types] / [damage_bonus]: the armor-type system this game does not have. The mod hooks the
// four call sites where a normal attack's damage is decided and multiplies it by the bonus of (attacker's weapon
// type, target's armor type). Spells are never touched. Evidence: docs/research/damage.md
namespace damagetypes {

// Redirects the four call sites after checking every one of them byte for byte. All four or none.
bool InstallHooks(uintptr_t exeBase);
bool Installed();

// The whole rule, exposed for the selftest: damage x bonus, rounded to nearest, 0 stays 0, result clamped 1..255.
// Returns `damage` unchanged in a multiplayer game, without a bonus table, or when either pointer is not a unit.
int Scale(int damage, game::Unit* attacker, game::Unit* target);

// TEST ONLY: what the thunks call instead of the game's own functions.
using RollFn = int(__cdecl*)(game::Unit*);
using TowerFn = int(__cdecl*)(game::Unit*, game::Unit*);
void SetOriginalsForTest(RollFn roll, TowerFn tower);
// TEST ONLY: the thunks themselves, so a test can drive them without the engine.
int RollThunkForTest(game::Unit* attacker);
int TowerThunkForTest(game::Unit* attacker, game::Unit* target);
int SplashScaleForTest(uint8_t* missile, game::Unit* source, game::Unit* victim, int damage);

}  // namespace damagetypes
