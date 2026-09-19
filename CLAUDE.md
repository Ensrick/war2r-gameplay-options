# war2r-gameplay-options (mod name: Gameplay Options)

Native autocast + tweaks mod for Warcraft II: Remastered (single-player). `version.dll` proxy, 32-bit MSVC, CMake.
The user named the mod "gameplay_options" on 2026-09-18 (everything mod-level carries that name; "autocast" now only
means the spell-casting feature: `[autocast]` section, `src/autocast.*`, the Ctrl+F9 banner). GitHub:
Ensrick/war2r-gameplay-options (private).

- Build: `.\build.ps1` -> `build\Release\version.dll` + `selftest.exe`. Deploy: `.\deploy.ps1` (`-Disable` to turn off).
  Package: `.\package.ps1` -> `dist\*.zip` (drag-and-drop layout).
- NEVER launch the game from a session. Stage, then ask the user to start it from Battle.net.
- Run `selftest.exe "<game exe>"` and `test\proxy_load_test.ps1` before every deploy. A failed build leaves the OLD
  selftest.exe in place: check the build output for errors before trusting "ALL CHECKS PASSED".
- Every address lives in `src/game.h` as an RVA; evidence in `docs/RE_NOTES.md` + `docs/research/*.md`. Do not add an
  address without decompile evidence. After ANY game patch the PE timestamp gate makes the mod inert; re-derive all
  RVAs (mana-table xref walk for the AI code, `rez\unitdata.dat` string xref for the data tables), update
  `kPeTimestamp`, re-run `selftest.exe`.
- Multiplayer gates (`kRvaNetGame` per tick, `kRvaNetGameAtLoad` at map load) are hard safety rules. Never remove them
  or make them configurable.
- Orders: `SetOrder` writes the NEXT-order byte (+0x2F). Always read orders through `game::EffectiveOrder`.
- Map-start data tweaks run ONLY from the new-map hook (0x4D2C46). Never apply table edits from the tick: savegames
  store the tables, so that double-applies.
- Versioning: public CHANGELOG starts at 1.0.0. Pre-release builds are `1.0.0-dev.N` (`MOD_PRERELEASE` in
  CMakeLists.txt); every change bumps N with an entry in `docs/DEV_HISTORY.md`. Pending work = GitHub issues.
- Updating the player's installed config after a default-file change: `py -3 tools/port_config.py <installed> <new
  default> <out> --old-default <default it came from>` keeps every value they changed and appends their tables. Never
  overwrite an edited config with the default.
- New config keys go in four places: `config.h`, `config.cpp` (reader + `kKnown` list),
  `tools/write_default_toml.py` (it GENERATES `config/gameplay_options.default.toml`: edit the generator, run it, never
  hand-edit the TOML), `docs/CONFIG_TUTORIAL.md`.
- Ghidra: `C:\Tools\ghidra_projects` (`war2bne`, `war2r`, clones `war2r_a` / `war2r_b` for parallel agents), helper
  `scripts\gh_run.ps1`. One headless run per project at a time. Ghidra's launcher breaks on paths with `(x86)`; exe
  copies live in `C:\Tools\ghidra_projects\bin`.
- Bash heredocs mangle backslashes and `\n` inside Python snippets: write patch scripts with the Write tool first.
