# Changelog

## 0.1.2 - 2026-09-18 (UNTESTED in game)

- Haste only goes on your flying units (`haste_flyers_only = 1`, new default), when they have an attack order or
  are fighting. Set it to 0 for the old rule (any fighting unit).

## 0.1.1 - 2026-09-18 (UNTESTED in game)

- Polymorph: flying attackers (dragon, gryphon rider, daemon, Deathwing) always qualify and are picked first, then
  enemy casters, then big ground units. Before this a daemon was skipped: its max HP in `Data\Rez\unitdata.dat` is 60,
  under the default `polymorph_min_hp = 90`. Dragons (100 HP) already qualified.

## 0.1.0 - 2026-09-18 (UNTESTED in game)

- First build. `version.dll` proxy for Warcraft II Remastered 1.0.2.2818, hooks the per-step AI tick.
- Autocast for Heal, Exorcism, Slow, Polymorph, Bloodlust, Death Coil, Haste; Unholy Armor available, off by default.
- `autocast.ini` with hot reload, `Ctrl+F9` in-game toggle, `autocast.log`.
- Inert in multiplayer, in any other exe build, and in the launcher helper exes that share the folder.
- Offline `selftest.exe` (hook bytes + targeting logic against the real exe image) and 32-bit proxy load test pass.
