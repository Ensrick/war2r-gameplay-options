# Gameplay Options for Warcraft II: Remastered

Autocast and gameplay tweaks for **Warcraft II: Remastered**, single-player. One `version.dll` next to the game exe,
one `gameplay_options.toml` for every setting.

- Player docs: [docs/USER_README.txt](docs/USER_README.txt) (ships in the zip), [docs/CONFIG_TUTORIAL.md](docs/CONFIG_TUTORIAL.md)
- Nexus page draft: [docs/NEXUS_DESCRIPTION.md](docs/NEXUS_DESCRIPTION.md)
- Reverse engineering: [docs/RE_NOTES.md](docs/RE_NOTES.md) and [docs/research/](docs/research)
- History: [CHANGELOG.md](CHANGELOG.md) (public, starts at 1.0.0), [docs/DEV_HISTORY.md](docs/DEV_HISTORY.md) (pre-release builds)

## Features

| Area | What | Config section |
|---|---|---|
| Autocast | Heal, Slow, Bloodlust, Raise Dead (on by default); Exorcism, Polymorph, Death Coil, Haste, Unholy Armor (opt-in) | `[spells] [autocast] [heal] [polymorph] [haste]` |
| Eye of Kilrogg | idle ogre-magi cast it, the eye scouts unexplored ground by itself | `[eye_of_kilrogg]` |
| Workers | idle workers repair nearby damage, then return to the nearest mine or tree | `[workers]` |
| Gold mines | optional unlimited mines, off by default | `[gold_mines]` |
| Heroes | 1 HP per second regeneration | `[heroes]` |
| Map-start data | health / price / time multipliers (master x race x group, all 1.0 by default, health is units-only), per-unit base stats, Longbow / Lighter Axes range bonus | `[health] [costs] [time] [unit.NAME] [range]` |

Everything is gated off in network games (local state changes and direct orders would desync a match) and on any exe
other than build 1.0.2.2818 (PE timestamp check, then per-hook byte checks).

## Build, test, deploy, package

```powershell
.\build.ps1          # VS 2022 with the x86 C++ toolchain. Output: build\Release\version.dll + selftest.exe
.\build\Release\selftest.exe "C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
.\test\proxy_load_test.ps1
.\deploy.ps1         # copies version.dll into <game>\x86\   (-Disable renames it away)
.\package.ps1        # dist\War2R-Gameplay-Options-<version>.zip, drag-and-drop layout (x86\...)
```

`selftest` maps the real exe as an image (none of its code runs), checks every hook site and table entry the mod relies
on, and drives all features over a fake world built inside the image's own globals.

## How it works

- `version.dll` proxy: the game imports VERSION.dll, which is not a KnownDLL, so Windows loads ours from the game
  folder; all 17 exports forward to the system DLL.
- Hook 1, `call` at `0x4E89A6` inside the per-step AI tick: runs the mod once per simulation step on the game thread.
- Hook 2, `call FinalizeTables` at `0x4D2C46`: new-map-only moment where unit / upgrade data is loaded but no unit
  exists yet. Savegame loads go through a different call site and are never touched, so nothing double-applies.
- Orders are issued through the game's own `IssueOrder` with the entries of its order handler table, the same path the
  computer AI and the player's command executor use.

## Layout

```
src/dllmain.cpp     proxy exports, attach
src/hook.cpp        the two call-site hooks
src/mod.cpp         per-tick orchestration, multiplayer gate, hotkey, config reload
src/autocast.cpp    spell targeting        src/eye.cpp        Eye of Kilrogg
src/workers.cpp     idle workers           src/tweaks.cpp     hero regen, gold mines
src/datatweaks.cpp  map-start table edits  src/config.cpp     TOML (toml++ vendored in third_party/)
src/game.h          every address (RVA) with its meaning; evidence in docs/
config/gameplay_options.default.toml   the default config, embedded into the DLL as a resource and shipped in the zip
```
