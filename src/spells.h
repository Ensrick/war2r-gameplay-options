#pragma once
#include "config.h"

// [spell_cost], [spell_damage], [mana] regen: the game's mana cost table (plain data) and the spell damage / heal cap /
// mana regeneration numbers, which are instruction immediates patched in place. Evidence: docs/research/spells.md
namespace spells {

// The game's damage per hit (heal: hit point cap per cast) and the most each patch site can hold: missile damage is a
// byte whose random roll must stay below 256, death coil's budget sits in signed byte compares, the rune subtraction is
// a signed byte.
constexpr int kDamageBase[kSpellDamageCount] = {40, 4, 10, 10, 4, 50, 50, 40};
constexpr int kDamageMax[kSpellDamageCount] = {254, 254, 254, 254, 254, 127, 128, 255};
constexpr int kMaxCost = 255;  // mana is a byte: a dearer spell could never be cast

// Runs from the new-map hook: Sync, plus one log line of every number that differs from the game's.
void OnNewMap(bool multiplayer);

// Brings the cost table and every patch site in line with the config; the game's own bytes in a multiplayer game or
// where a value equals the game's. Only writes what differs, so it runs every step (a loaded save or a hot reload is
// picked up within one step, and a multiplayer game started from a savegame is restored before its first unit update).
// A site whose bytes are neither the game's nor the mod's last write is refused for the session and logged once.
void Sync(bool multiplayer);

unsigned WriteCount();  // table words and patch groups written since start (selftest)

}  // namespace spells
