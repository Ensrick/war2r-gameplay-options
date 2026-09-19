# Changelog

## 1.0.8 - staged 2026-09-18, not uploaded yet

- `[health] all` now covers EVERYTHING (units, ships and structures), exactly like `[costs] all` and `[time] all`.
  New `[health] units` (and `[health.human] units` / `[health.orc] units`) is the units-only master.
- **Changed meaning:** up to 1.0.7 `[health] all` meant units only. If you had set it, move the number to
  `[health] units` to keep your buildings as they were. `tools/migrate_config.py` (GitHub) does it in place.

## 1.0.7 - staged 2026-09-18, not uploaded yet

- Fixed: autocast never saw flying units. The game keeps flyers in a separate air layer that the mod did not scan, so
  no Bloodlust, Haste or Heal went onto your dragons and gryphon riders, no Death Coil, Polymorph, Slow or Exorcism
  onto enemy flyers, and an enemy flyer did not count as "enemy nearby". Both layers are scanned now.

## 1.0.6 - staged 2026-09-18, not uploaded yet

- New `[oil_platforms] unlimited` (off by default): oil platforms never run dry, the computer's too. Independent of
  `[gold_mines] unlimited`, which never covered oil.

## 1.0.5 - staged 2026-09-18, not uploaded yet

- `[building.watch_tower]` and `[building.orc_watch_tower]` are accepted for the orc scout tower (the game calls it
  Watch Tower), `[building.scout_tower]` for the human one.

## 1.0.4 - staged 2026-09-18, not uploaded yet

- Readme and Nexus description: what to check when nothing happens and no `gameplay_options.log` appears (the game
  that was started is not the install the mod is in). No change to the mod's behaviour.

## 1.0.3 - staged 2026-09-18, not uploaded yet

- "All structures" multipliers: a `structures` key at the top of `[health]`, `[costs]` and `[time]` (both races) and
  under each race table (umbrella over `buildings` and `building_upgrades`). `[costs]` and `[time]` also gain top-level
  `units` and `research` masters. All 1.0 by default; in `[health]` the `all` values stay unit-only.
- The shipped `gameplay_options.toml` is reorganised into ten numbered parts with a banner each and a contents list at
  the top; every race table has UNITS / STRUCTURES / RESEARCH sub-headers.
- Neutral structures (gold mine, dark portal, runestone) are never scaled.

## 1.0.2 - staged 2026-09-18, not uploaded yet

- Quieter defaults, on the author's call that players should not have to switch features off: only Heal, Slow,
  Bloodlust and Raise Dead autocast and worker auto-repair are on out of the box. Exorcism, Polymorph, Death Coil,
  Haste, Eye of Kilrogg (cast and auto-scout), worker auto-harvest and hero regeneration default to off.
- The unit examples in the shipped config (dragon / gryphon rider sight 8, the destroyer) are comments.
- An existing `gameplay_options.toml` is never overwritten, so an updating player keeps their behaviour.

## 1.0.1 - built 2026-09-18, never uploaded (ships together with 1.0.2)

- `[building.<name>]` tables: the same ten base stats as `[unit.<name>]` for every structure (hit points, armor,
  basic and piercing damage, range, sight, gold, lumber, oil, build time). Towers moved here from the unit names.
- `[health.human]` / `[health.orc]` gain `buildings` and `building_upgrades`. Structure health follows only these
  two keys; the `all` masters and the unit groups stay unit-only.
- Unit price multipliers now scale oil as well as gold and lumber.
- The `[heroes] units` list is grouped by side, and unit names accept short and in-game spellings
  (`uther`, `grom`, `grommash_hellscream`, `teron`, `korgath`, `gryphon` ...).

## 1.0.0 - 2026-09-18 - Initial release (published by the author on Nexus, build dev.23)

For Warcraft II: Remastered 1.0.2.2818, single-player.

Autocast
- Paladin: Heal (units missing 10+ HP, most hurt first), Exorcism.
- Mage: Polymorph with a configurable, prioritised target list (dragons, gryphon riders and daemons first), Slow.
- Ogre-Mage: Bloodlust on units that are fighting; Eye of Kilrogg when idle at full mana, and the eye scouts
  unexplored ground by itself.
- Death Knight: Raise Dead when enemies are near, Death Coil, Haste on flyers, Unholy Armor (off by default).
- Move orders are never interrupted, casters never double up on a target, Ctrl+F9 toggles autocast in game.
- Left manual on purpose: Fireball, Flame Shield, Invisibility, Blizzard, Death and Decay, Whirlwind, Runes.

Workers
- Idle workers repair damaged buildings nearby (1 s, 10 tiles), then return to the nearest gold mine or tree
  (10 s, 5 tiles). Loaded workers deliver first. Stand Ground is respected.

Tweaks (all configurable)
- Unlimited gold mines, off by default.
- Heroes regenerate 1 HP per second.
- Multipliers for health, prices and build / research time, 1.0 by default: master x race (human / orc) x group.
  Health only touches units. Caps are the engine's: 65535 hit points, 2550 per unit or structure price, 65535 per
  research price, 255 per time.
- Per-unit base stats in `[unit.<name>]` tables: hit points, armor, basic / piercing damage, range, sight, gold,
  lumber, oil, build time. Ships with sight 8 for dragons and gryphon riders and a worked destroyer example.
- Configurable range bonus for Longbow / Lighter Axes.

Config
- `gameplay_options.toml`, hot reloaded, with error reporting that never breaks a running game.

Pre-release build notes: `docs/DEV_HISTORY.md`.
