#pragma once
#include "config.h"

// [upgrades]: what one level of a weapon, armor or siege upgrade is worth. The game keeps one byte per upgrade
// group at 0x8C11DC and the damage code multiplies it by the player's level counter, so a single byte moves every
// unit of that line at once. Evidence: docs/research/damage.md
namespace upgrades {

// The game's own bytes, in [upgrades] key order (missile, melee, shields, ship damage, ship armor, siege).
constexpr int kVanilla[kUpgradeEffectCount] = {2, 2, 2, 5, 5, 15};
constexpr int kMaxEffect = 100;  // the damage path clamps at 255 and a counter reaches 2, so this can never wrap

// Runs from the new-map hook: Sync, plus one log line of every byte that differs from the game's.
void OnNewMap(bool multiplayer);
// Brings the effect table in line with the config; the game's own bytes in a multiplayer game or where a key is -1.
// Only writes what differs, so it runs every step (a loaded save or a hot reload is picked up within one step).
// A byte that is neither the game's nor the mod's last write is left alone for the session and logged once.
void Sync(bool multiplayer);

unsigned WriteCount();  // bytes written since start (selftest)
void ResetForTest();    // TEST ONLY: forget the one-shot table check and every refusal

}  // namespace upgrades
