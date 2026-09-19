# war2r-gameplay-options (mod name: Gameplay Options)

Native autocast + tweaks mod for Warcraft II: Remastered (single-player). `version.dll` proxy, 32-bit MSVC, CMake.
The user named the mod "gameplay_options" on 2026-09-18 (everything mod-level carries that name; "autocast" now only
means the spell-casting feature: `[autocast]` section, `src/autocast.*`, the Ctrl+F9 banner). GitHub:
Ensrick/war2r-gameplay-options (public since 2026-09-18).

- Build: `.\build.ps1` -> `build\Release\version.dll` + `selftest.exe`. Deploy: `.\deploy.ps1` (`-Disable` to turn off).
  Package: `.\package.ps1` -> `dist\*.zip` (drag-and-drop layout).
- NEVER launch the game from a session. Stage, then ask the user to start it from Battle.net.
- Run `selftest.exe "<game exe>"`, `test\proxy_load_test.ps1` and `test\activation_test.ps1` before every deploy. A failed build leaves the OLD
  selftest.exe in place: check the build output for errors before trusting "ALL CHECKS PASSED".
- Every address lives in `src/game.h` as an RVA; evidence in `docs/RE_NOTES.md` + `docs/research/*.md`. Do not add an
  address without decompile evidence. After ANY game patch the PE timestamp gate makes the mod inert; re-derive all
  RVAs (mana-table xref walk for the AI code, `rez\unitdata.dat` string xref for the data tables), update
  `kPeTimestamp`, re-run `selftest.exe`.
- "The mod does nothing" report: FIRST check that `x86\gameplay_options.log` exists and is newer than the session. No
  log = the DLL never activated = wrong exe (a second install elsewhere on the disk, started from an old shortcut, is
  the usual reason); compare the file LastAccessTime of both installs to see which one ran.
- Multiplayer gates (`kRvaNetGame` per tick, `kRvaNetGameAtLoad` at map load) are hard safety rules. Never remove them
  or make them configurable.
- Units sit in TWO tile grids: land / sea at `0x91AD6C`, flyers ONLY in the air grid `0x91AD70`. Any per-tile lookup
  must read both (`ScanGridRaw` does). The selftest's `AddUnit` files flyers into the air grid for that reason.
- Orders: `SetOrder` writes the NEXT-order byte (+0x2F). Always read orders through `game::EffectiveOrder`.
- Map-start data tweaks run ONLY from the new-map hook (0x4D2C46). Never apply table edits from the tick: savegames
  store the tables, so that double-applies.
- Versioning (repo PUBLIC since 2026-09-18, author: "increment the changelog properly"): semantic versioning. Patch =
  fixes / docs, minor = new settings or features, major = breaks existing config files. EVERY change gets its own
  version in CMakeLists.txt, a dated entry in `CHANGELOG.md` (keep its "On Nexus Mods right now" line true), the same
  entry in `nexus/NEXUS_CHANGELOG.txt` + `nexus/changelog_<ver>_api.txt`, and the why in `docs/DEV_HISTORY.md`.
  Everything in the repo and the issues is public: no local paths, user names or keys. Pending work = GitHub issues.
- License: MIT, Copyright (c) 2026 Ensrick (the author's final call on 2026-09-18, after trying a restrictive text in
  1.0.9). The project is open source; toml++ in third_party is MIT too.
- A config written by an older version: `py -3 tools/migrate_config.py <file>` brings it up to date in place without
  changing behaviour (adds `[oil_platforms]`, moves a pre-1.0.8 `[health] all` into `units`). Extend it whenever a key
  is added or changes meaning.
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
