# Changelog

## 1.0.1 - staged 2026-09-18, not uploaded yet

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
