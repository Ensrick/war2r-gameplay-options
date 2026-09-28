#pragma once

// [general] fog_of_war: the game forgets its own Options screen choice on every start and takes a savegame's or a
// custom game's instead, so the player's choice is applied once at the start of every single-player game.
// Evidence: docs/research/fog_of_war.md
namespace fog {

void OnNewMap();  // from the new-map hook (single-player maps only): apply on the first tick of the map
void OnTick();    // every simulation step, single-player only
unsigned ApplyCount();  // times the flag was written this session (tests)

}  // namespace fog
