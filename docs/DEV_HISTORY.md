# Development history (pre-release builds)

Builds made before the first public release. The public changelog (CHANGELOG.md) starts at 1.0.0.
Every change gets its own build number; newest first.

## 1.0.0-dev.7 - 2026-09-18 (UNTESTED in game)

- Hero regeneration (#6): `[heroes] regen_hp_per_second = 1`, `regen_for = "all" | "mine"`, and a configurable
  `units` list that defines what a hero is (only 5 of the 15 hero types carry the game's own hero flag). Paced by real
  time while the simulation is stepping; pauses and loads are not credited.
- Internal split: `world.*` (game snapshot + unit helpers), `mod.*` (tick orchestration, multiplayer gate),
  `tweaks.*` (non-spell features). The multiplayer gate now covers every feature, not only casting.

## 1.0.0-dev.6 - 2026-09-18 (UNTESTED in game)

- Config moved from `autocast.ini` to `autocast.toml` (toml++ 3.4.0 vendored). New `[polymorph] targets` list is the
  whole Polymorph rule: listed unit types only, earlier in the list wins, nearest first. The default list reproduces
  the previous behaviour (flyers, then casters, then 90+ HP ground units); `polymorph_min_hp` is gone.
- Unknown sections/keys, wrong value types and out-of-range numbers are reported in `autocast.log`; a syntax error
  keeps the previous settings and shows an in-game banner. A successful hot reload shows a banner too.
- Version scheme: public changelog starts at 1.0.0, pre-release builds are 1.0.0-dev.N.

## 0.1.5 - 2026-09-18 (UNTESTED in game)

- Fix (found by decompile review before any in-game run): the game's SetOrder (`FUN_004ef080`) writes a new order to
  the unit's NEXT-order slot (+0x2F); it only becomes the current order (+0x2E) when the running action step ends
  (`0x4ED9C3`). The mod checked +0x2E, so every cast looked like a failure: a paladin's heal fell through to exorcism
  in the same pass, no target was ever claimed, and casts were re-issued every pass. All order reads now use the
  effective order (next if set, else current), the same rule the game's UI uses at `0x4E85B2`.
- A move the player has just queued is protected exactly like a move under way.

## 0.1.4 - 2026-09-18 (UNTESTED in game)

- Heal only goes on units missing at least 10 HP (`heal_min_missing_hp = 10`), so paladins stop casting on scratches.
  The percent gate stays as an option but now defaults to off (`heal_below_pct = 100`).

## 0.1.3 - 2026-09-18 (UNTESTED in game)

- Raise Dead added for death knights (`raise_dead = 1`). Cast at the nearest corpse tile, the way the game AI does
  it (order 0x32 at x/y, no target unit), and only while an enemy is within `search_radius`. It runs first in the
  death knight's priority list, matching the AI: raise dead, unholy armor, death coil, haste.
- Fireball, Flame Shield, Invisibility, Blizzard, Death and Decay, Whirlwind and Runes are deliberately left manual.

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
