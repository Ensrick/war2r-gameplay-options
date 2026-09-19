#pragma once

// Edits to the game's unit / upgrade data tables (health, prices, sight). Evidence: docs/research/data_tables.md
namespace datatweaks {

// Runs from the hooked `call FinalizeTables` at 0x4D2C46: once per NEW map, after the map's (or the default) unit and
// upgrade data is in the tables and before any unit exists. It never runs for a savegame load, and that is the point:
// a save carries the tables it was made with, so multiplying there would double-apply.
void OnNewMapTablesLoaded();

// Keeps the Longbow / Lighter Axes range bonus (a 2-byte code patch) equal to [range] upgrade_bonus, or to the
// game's own +1 in a multiplayer game. Cheap enough to call every tick; it only writes when the value is off.
void SyncRangeBonus(bool multiplayer);

}  // namespace datatweaks
