#pragma once

// Edits to the game's unit / upgrade data tables (health, prices, sight). Evidence: docs/research/data_tables.md
namespace datatweaks {

// Runs from the hooked `call FinalizeTables` at 0x4D2C46: once per NEW map, after the map's (or the default) unit and
// upgrade data is in the tables and before any unit exists. It never runs for a savegame load, and that is the point:
// a save carries the tables it was made with, so multiplying there would double-apply.
void OnNewMapTablesLoaded();

}  // namespace datatweaks
