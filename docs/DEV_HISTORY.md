# Development history (pre-release builds)

Builds made before the first public release. The public changelog (CHANGELOG.md) starts at 1.0.0.
Every change gets its own build number; newest first.

## 1.0.0-dev.16 - 2026-09-18 (UNTESTED in game)

- `[costs]` is now a full set of price groups, all 1.0 by default: `units`, `buildings`, `building_upgrades`
  (types 0x58-0x5B keep / stronghold / castle / fortress and 0x60-0x63 guard / cannon towers: a structure upgrade is
  priced as the type it turns into, same pay path `FUN_004ac610`), and research by group: `melee_upgrades` (UGRD rows
  0-3, 8-11), `ranged_upgrades` (4-7, 24-31), `siege_upgrades` (20-23), `paladin_ogre_mage_upgrades` (32-36, 43, 44,
  50), `naval_upgrades` (12-19), `mage_death_knight_spells` (37-42, 45-49, 51).
- Buildings and building upgrades scale gold, lumber and oil; units stay gold and lumber only. Free rows stay free.
- Structure ids and prices checked against `Data\Rez\unitdata.dat` (tower flag 0x100000 on 0x60-0x63, hall flag on
  0x4A, 0x4B, 0x58-0x5B).

## 1.0.0-dev.15 - 2026-09-18 (UNTESTED in game)

- Health and price multipliers now default to 1.0 (the game's own numbers) and only ever touch units: the
  `[health] buildings` key is gone and structure rows are never read or written. Accepted range 0.01 to 1000.
- Caps are the engine's storage limits instead of a cautious 9999: health 65535 (16-bit word; damage code verified
  unsigned, see the addendum in docs/research/data_tables.md), unit price 2550 (one byte of tens), upgrade price 65535.

## 1.0.0-dev.14 - 2026-09-18 (UNTESTED in game)

- Vision (#7): `[vision]` maps unit names to extra sight range, default `dragon = 2`, `gryphon_rider = 2` (6 -> 8).
  Clamped at 9: the engine has reveal functions for sight 0..9 only, and a foreign pointer in the sight table would
  crash the next savegame load.

## 1.0.0-dev.13 - 2026-09-18 (UNTESTED in game)

- Upgrade prices (#8): `[costs] ranged_upgrades = 0.5` (UGRD rows 4-7, 24-31: arrows, throwing axes, ranger / berserker
  upgrade, longbow, lighter axes, scouting, marksmanship, regeneration) and `siege_upgrades = 0.5` (rows 20-23).

## 1.0.0-dev.12 - 2026-09-18 (UNTESTED in game)

- Unit prices (#9): `[costs] units = 0.5` scales the gold and lumber byte tables (price / 10) for unit types below
  0x3A. Rounds to the nearest 10, a priced unit never becomes free, buildings and oil are untouched.

## 1.0.0-dev.11 - 2026-09-18 (UNTESTED in game)

- Second hook: the new-map-only `call FinalizeTables` at `0x4D2C46`. The mod edits the unit / upgrade tables there:
  after the map's or the default data (and Blizzard's hard-coded overrides) are loaded, before any unit exists, and
  never on a savegame load, so nothing can double-apply (saves store the tables). Gated on the load-time network
  flag `0x922F5B`. Research: docs/research/data_tables.md. If this hook does not match, only these tweaks are lost.
- Health (#5): `[health] units = 2.0`, `heroes = 4.0`, `buildings = 1.0`. Capped at 9999; gold mine, dark portal and
  runestone are never scaled. Heroes are the `[heroes] units` list.
- Config is now loaded by whichever hook fires first (the map-load hook runs before the first tick).

## 1.0.0-dev.10 - 2026-09-18 (UNTESTED in game)

- Idle workers (#3, #4): a peasant / peon of yours that sits in STOP with nothing queued repairs the nearest damaged,
  finished building of yours within `repair_radius` (10) after `repair_idle_seconds` (1), otherwise after
  `harvest_idle_seconds` (10) walks to the nearest gold mine or reachable tree within `harvest_radius` (5). A loaded
  worker returns its cargo instead (a harvest order would relabel carried gold as lumber). Stand Ground is respected.
  Repair needs 1+ gold and 1+ lumber (the game stops the worker with a message otherwise); construction sites are
  skipped (power-build exploit). Orders use the game's own handler table entries 23 / 24 / 27 and clear the Remastered
  resume-order byte the way the player command path does. Research: docs/research/workers_and_gold.md.
- selftest now verifies the handler table entries and the gold decrement instruction bytes in the real exe.

## 1.0.0-dev.9 - 2026-09-18 (UNTESTED in game)

- Unlimited gold mines (#2), `[gold_mines] unlimited = false` by default. No code patch: each pass the mine's
  "gold left" word (+0x82, hundreds) is restored to the most it held while watched, never below 50 (5000 gold, which
  also keeps the computer's expansion test satisfied). Applies to every mine on the map, the computer's included.

## 1.0.0-dev.8 - 2026-09-18 (UNTESTED in game)

- Eye of Kilrogg (#1): idle ogre-magi (also Dentarg and Cho'gall) cast it at `cast_at_mana` (default 255, the
  computer's own rule), at most `max_active` eyes at a time. Cast through the same IssueOrder + pending-spell path the
  player's own button uses (research: docs/research/eye_of_kilrogg.md).
- Auto-scout: the computer's eye only random-walks (AI order 4). The mod steers the player's eyes with the move
  handler toward tiles the local explored map (`0x91AD60`) marks as never seen, then toward fogged tiles, then wanders.
  An eye that comes to rest more than 2 tiles from where the mod sent it is treated as player-controlled and released.
- IssueOrder / IssueSpell moved into `world.*` for every feature to share.

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
