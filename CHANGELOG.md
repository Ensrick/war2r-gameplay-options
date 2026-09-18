# Changelog

## 1.0.0 - Initial release (in development, not yet published)

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
- Health multipliers for units and for heroes, 1.0 (unchanged) by default, capped at the engine's 65535.
- Dragons and gryphon riders: +2 sight.
- Price multipliers, 1.0 by default: units, buildings, building upgrades (cap 2550), and research by group: melee,
  elf / troll, siege, paladin / ogre-mage, naval, mage / death knight spells (cap 65535).

Config
- `autocast.toml`, hot reloaded, with error reporting that never breaks a running game.

Pre-release build notes: `docs/DEV_HISTORY.md`.
