#pragma once

// Edits to the game's unit / upgrade data tables (health, prices, sight). Evidence: docs/research/data_tables.md
namespace datatweaks {

// Runs from the hooked `call FinalizeTables` at 0x4D2C46: once per NEW map, after the map's (or the default) unit and
// upgrade data is in the tables and before any unit exists. It never runs for a savegame load, and that is the point:
// a save carries the tables it was made with, so multiplying there would double-apply.
void OnNewMapTablesLoaded();

// A config reload in the middle of a game: puts the tables back to what they were before the new-map pass (the copy
// that pass took of them) and applies the whole pass again with the new settings, so nothing is ever applied twice.
// Units alive keep the same fraction of their hit points when a type's maximum changes. Nothing happens in
// multiplayer, or when the running game came from a savegame (no copy of its original tables exists).
void OnConfigReloaded(bool multiplayer);

void ResetForTests();  // forget the snapshot and the "not now" log flag

// Keeps the Longbow / Lighter Axes range bonus (a 2-byte code patch) equal to [range] upgrade_bonus, or to the
// game's own +1 in a multiplayer game. Cheap enough to call every tick; it only writes when the value is off.
void SyncRangeBonus(bool multiplayer);

}  // namespace datatweaks
