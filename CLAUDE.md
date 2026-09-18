# war2r-autocast

Native autocast mod for Warcraft II: Remastered (single-player). `version.dll` proxy, 32-bit MSVC, CMake.
Repo name is a working name; the user has not named the mod yet.

- Build: `.\build.ps1` -> `build\Release\version.dll` + `selftest.exe`. Deploy: `.\deploy.ps1` (`-Disable` to turn off).
- NEVER launch the game from a session. Stage, then ask the user to start it from Battle.net.
- Every address lives in `src/game.h` as an RVA with its evidence in `docs/RE_NOTES.md`. Do not add an address without
  decompile evidence. After ANY game patch: the PE timestamp gate makes the mod inert; re-derive all RVAs (the
  mana-table xref walk in RE_NOTES is the fastest way back in), update `kPeTimestamp`, re-run `selftest.exe`.
- Run `selftest.exe "<game exe>"` and `test\proxy_load_test.ps1` before every deploy.
- Multiplayer gate (`kRvaNetGame`) is a hard safety rule: direct IssueOrder calls desync network games. Do not
  remove or make it configurable.
- Ghidra projects: `C:\Tools\ghidra_projects` (`war2bne`, `war2r`), helper `scripts\gh_run.ps1`. Ghidra's launcher
  breaks on paths containing `(x86)`; exe copies live in `C:\Tools\ghidra_projects\bin`.
- Every change gets its own version bump (`CMakeLists.txt` project VERSION) and CHANGELOG entry.
