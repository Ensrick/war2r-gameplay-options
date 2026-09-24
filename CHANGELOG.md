# Changelog

Semantic versioning: patch = fixes and documentation, minor = new settings or features, major = a change that breaks
existing `gameplay_options.toml` files. Every change gets its own version and entry here, newest first.

**Released: 1.16.2** on [Nexus Mods](https://www.nexusmods.com/warcraft2/mods/8) and as a
[GitHub release](https://github.com/Ensrick/war2r-gameplay-options/releases) (2026-09-20). Nexus carries a changelog
entry for every version since 1.0.0.

## 1.22.1 - 2026-09-23

- Fixed: autocast no longer targets enemies you cannot see: a submarine or turtle no detector of yours has in range,
  a unit under fog, an invisible unit. It uses the game's own per-unit visibility, the same test your own click goes
  through, so a sub your flying machine or zeppelin spots is a valid target again at once.

## 1.22.0 - 2026-09-23

- Blizzard and Death and Decay autocast now aim at the spot where the waves hit the most: every tile in casting range
  is scored by what the 5 x 5 impact pattern covers (enemy units, and the middle of enemy buildings, which is where
  the splash actually reaches them). No more casts on a building's corner that miss it; a spot with most of its
  pattern off the map is skipped. With log_casts the cast line says what it covers.

## 1.21.0 - 2026-09-23

- New `[auto_production.class_upgrade_bias]`: `upgrade_bias` for one class only. Example: `siege = 0.5` makes every
  ballista / catapult upgrade level raise their share twice as much as the default 0.25 does for the other lines. A
  class left out uses `upgrade_bias`; the table ships empty.

## 1.20.1 - 2026-09-22

- Fixed: `[heal] cooldown_for_computer` was read correctly but reported in the log as an unknown key. The react
  range log line now names the unit and both numbers.

## 1.20.0 - 2026-09-22

- New `[heal] cooldown_for_computer` (on): the computer's paladins keep to the same Heal / Exorcism cooldown as yours,
  with the same urgent-heal exception. The game's own rule heals any unit missing a single hit point on every think
  step, so with a cheaper Heal they never stopped. Set it to false to play them exactly as the game does. Single
  player only; nothing changes in a network game.

## 1.19.0 - 2026-09-22

- New `react_range` in `[unit.NAME]` / `[building.NAME]` (0 to 20, -1 = the game's): how far a unit looks for a target
  on its own, separate from how far it can shoot. A `range` above the game's reaction distance now raises the reaction
  distance to match (never lowers it), so a tower given range 9 opens fire at 9 instead of its old 6. Ships and land
  units already notice enemies further than they shoot, so their `range` alone was working.

## 1.18.0 - 2026-09-22

- New `[autocast] resume_orders` (on): a caster that was on an attack-move or a patrol when the mod cast a spell for
  it gets that order back once the spell is over, with the same destination, the way your own click would restore it.
  Since 1.16.2 the order had to be dropped before the cast (that was the crash); now it is only paused. A new order
  from you in the meantime wins, and nothing is given back after 30 seconds.

## 1.17.1 - 2026-09-22

- Checked after a report that the spell button shows a different cost than the cast drains: the button reads the
  same table the game charges from, so it always shows the mod's price. For Heal and Exorcism that price is PER HIT
  POINT (the game's rule): the button says what one hit point costs, and a heal that mends 30 hit points at
  heal = 2 drains 60 mana. The config comment and the tutorial say so now.

## 1.17.0 - 2026-09-22

- New `[heal] cooldown_seconds` (0 = off): seconds a paladin waits after a Heal or an Exorcism before autocasting
  either again, one timer per paladin. Two things break the wait: a heal target at or below `urgent_below_percent`
  (10) of its hit points, and an exorcism the paladin's mana can finish outright. With `log_casts` the cast line says
  why the wait was broken.

## 1.16.2 - 2026-09-21

- Fixed a crash: a paladin given Heal by autocast while it still had an attack-move to carry out (Remastered
  resumes such orders after a cast) crashed the game inside the Heal action; Flame Shield had the same hole. Every
  order the mod gives now clears the pending attack-move or patrol first, exactly as your own clicks do. A caster
  that autocasts therefore no longer resumes its attack-move afterwards; it stands and fights where it is.

## 1.16.1 - 2026-09-21

- The shipped config carries two worked examples of weapon and armor types, switched off: siege weapons and every
  ship against buildings and two kinds of hull, and arrows against workers and buildings. Remove the # signs to try
  one. Every rule is in the tutorial under "Your own weapon and armor types".

## 1.16.0 - 2026-09-21

- New: weapon types and armor types you define yourself. `[weapon_types]` and `[armor_types]` map names you invent to
  lists of units and buildings (or the ready-made groups `structures`, `ships`, `air_units`, `land_units`), and
  `[damage_bonus.<weapon type>]` gives each pair a multiplier from 0.0 to 10.0. Example in the file: siege weapons at
  x2 against structures and x1.5 against ships. Everything you leave out stays x1.0 and the sections ship empty.
- Splash damage (catapults, ballistas, cannon towers, ship guns) is scaled per victim: a shot aimed at a building hits
  the footmen next to it with their own multiplier. Spells are never affected. Walls take splash unscaled.
- The game itself has no armor types (docs/research/damage.md): this adds them. The computer's units follow the same
  types. Single player only: in multiplayer the hooks pass every hit through untouched.

## 1.15.0 - 2026-09-21

- New `[upgrades]` section: what one level of an upgrade adds. `missile_damage` (2), `melee_damage` (2), `shields`
  (2), `ship_damage` (5), `ship_armor` (5), `siege_damage` (15, catapults and ballistas); -1 keeps the game's number,
  otherwise 0 to 100. The unit panel in game shows the new bonus. These are the game's own tables, so the computer
  gets the same numbers. Single player only; the game's numbers come back in multiplayer.
- Research write-up of the damage formula in docs/research/damage.md. For the record: this game has no armor types or
  damage types. Piercing damage ignores armor, basic damage is reduced by it, the hit is 50 to 100 % of the sum.

## 1.14.2 - 2026-09-20

- Fixed in auto-production: units that are inside a building were not counted. A tanker inside its oil platform or the
  shipyard counted as "no tanker", so a second tanker was trained; peasants inside the gold mine or the hall were
  missing from the worker count, so more workers were trained than the target. Units inside mines, platforms, halls
  and transports are counted now.

## 1.14.1 - 2026-09-20

- Tutorial: `plenty_units`, `bank_multiple` and `filler_min` all count units of bank and are easy to mix up; the
  auto-production chapter now says which is a weight, which is a gate and which is neither.

## 1.14.0 - 2026-09-20

- New `[auto_production] plenty_units` (10): being able to pay for this many of a unit counts as plenty. At or above
  it the class gets its full weight and the size of your bank stops mattering, so with plenty of everything the army
  is exactly your weights; below it the share shrinks in proportion. This was a built-in 5 until now.
- New configs ship 10. `migrate_config.py` writes 5 into a config that already had auto-production set up, so an
  existing game keeps its mix until you change the number yourself.

## 1.13.0 - 2026-09-20

- Blizzard and Death and Decay autocast now work next to your own army. Until now the mod only tried the target's own
  tile and gave up when anything of yours stood within 4 tiles of it, which on land is nearly always (your troops
  stand next to what they attack). It now moves the aim a tile or two off the target, away from your units, and casts
  where nothing of yours can be hit: the waves scatter over a 5 x 5 block around the aim, so the target is still hit.
  A clean straight aim is still preferred.
- New `[autocast] area_friendly_clearance` (4): the distance in tiles that must hold nothing of yours. 4 keeps
  everything of yours out of reach, 3 allows a rare glancing hit, 0 ignores your own units like the computer does. The
  watchdog that stops a running channel uses the same number.
- The aim must be within the spell's range of where the caster stands: the mod no longer issues a Blizzard or Death
  and Decay the caster would have to walk to.
- With `log_casts` on, the log says why a Blizzard or Death and Decay was not cast: no enemy in reach, the spot is not
  worth it, one of your units is in the way (which one, where), another area spell is already there, one wave would
  finish the buildings, or mana.

## 1.12.0 - 2026-09-20

- Fixed in auto-production: a cheap unit could starve a dearer one of a scarce resource for ever. With little oil, a
  shipyard bought a destroyer every time the oil allowed one, so the oil never reached what a battleship needs and the
  fleet ended up all destroyers, whatever the mix said. A building whose most-wanted class is short of money only now
  keeps that resource and saves up. Classes that do not cost the blocked resource are still trained (grunts keep
  coming while knights wait for lumber; land units never wait for oil).
- New `[auto_production] save_up_seconds` (60): saving up ends after this long without the blocked resource growing,
  and one unit of the cheaper class is released, so a stalled economy never stops a building. 0 = the old behaviour.
- The log (with `log_casts`) says what a building is saving for, and "saving" is a reason in the "nothing was built"
  line.

## 1.11.0 - 2026-09-20

- New `[priority]` section: the order each caster tries its spells in (`paladin`, `mage`, `ogre_mage`,
  `death_knight`), as lists of spell names. The defaults are the order the mod has always used, so nothing changes
  until you edit a list.
- New `[priority] save_mana` (on): a caster that could cast a spell higher in its list, if only it had the mana, casts
  nothing lower and saves up for it. This is what lets Death and Decay and Blizzard happen at all next to cheaper
  spells: a channel needs mana for three waves, and a death knight used to spend every 50 mana on Death Coil first. A
  spell with no target never holds anything back. Note for existing setups: with the default lists this also means a
  mage with a Polymorph target in reach saves for Polymorph instead of casting Slow.
- With `log_casts` on, the log says when a caster starts saving and for what.

## 1.10.1 - 2026-09-20

- The log of the last two game sessions is kept: starting the game renames `gameplay_options.log` to
  `gameplay_options.prev.log` (and that one to `gameplay_options.prev2.log`) instead of overwriting it, so the log of
  a game that went wrong is still there after a restart.

## 1.10.0 - 2026-09-20

- New `[general] log_ai` (off by default): a read-only diagnostic for the question "why did the computer stop
  attacking?". Once a minute every computer player writes one line to the log: where its script is, what it is waiting
  for, its gold, lumber, oil and food, and its army numbers against the numbers its script wants. A player that has
  sat on the same wait for 5 minutes gets an extra line. It reads the game and changes nothing.
- Research write-up of the computer player's script interpreter in docs/research/ai_stall.md (#24). The cause of the
  reported stall is not known yet; this log is how it gets caught.

## 1.9.0 - 2026-09-20

- Smarter Blizzard and Death and Decay autocast: enemy buildings are targets now. The mod picks the spot worth the
  most, where a building counts as `area_building_value` units (new `[autocast]` key, 3 by default), and aims at the
  middle of a building rather than its corner.
- It casts on one enemy building or more, or on `area_min_enemies` enemy units (3) when no building is in the blast;
  two lone units are no longer worth a channel. Whirlwind keeps its old rule.
- No overkill: a channel aimed at buildings stops once the waves already cast cover the hit points those buildings
  had, and none is started on buildings a single wave would flatten. Friendly units, buildings and walls in the blast
  area still block the cast, as before.
- The readme no longer calls the area spells manual-only and lists the current worker targets (12 / 16 / 24).

## 1.8.0 - 2026-09-20

- New `[auto_production.no_enemy_navy_cap]`: while no hostile player owns a shipyard or a warship anywhere on the map
  (fog does not matter, an unfinished shipyard counts), auto-production holds at most 1 tanker, 5 destroyers, 2
  battleships / juggernaughts and 2 submarines, and builds no further ships. The share that would have gone to ships
  goes to your land army instead. The caps lift the moment an enemy shipyard or warship exists.
- The log says once when the caps come into force and once when they lift, and "cap" is a reason in the
  "nothing was built" line.

## 1.7.3 - 2026-09-19

- The auto-production mix numbers are weights, not percentages: only their ratios matter and a tier's numbers need
  not add up to 100. Said so in the config file and the tutorial; nothing changed in how it builds.
- Fixed: the "production: map is N % water" line was logged again whenever terrain changed (a regrown tree was
  enough), because the map fingerprint followed every flag bit instead of the water alone.

## 1.7.2 - 2026-09-19

- Fixed: auto-production never trained workers on a poor start. They had to clear the upgrade reserve plus four times
  their price, and workers are what earn that money. Workers and the single oil tanker now wait for their own price
  only (`workers_ignore_reserve`, `tankers_ignore_reserve`, both on).
- Worker targets are three numbers now: `workers_tier1` 12, `workers_tier2` 16, `workers_tier3` 24, replacing
  `workers_per_hall_tier`.
- The upgrade reserve is no longer dictated by the keep: only a research can be its dearest item, while keeps, castles
  and towers count at the `reserve_extra` weight. With a hall, barracks and blacksmith that is 1300 gold instead of
  2200, so the army starts sooner while the buffer you asked for stays.
- With `[general] log_casts` on, a line every 30 s when nothing was built, naming the first blocked gate per unit
  type, your bank and the reserve.

## 1.7.1 - 2026-09-19

- Auto-production: the upgrade reserve no longer holds money back for a research whose only building is already
  paying for another one, so the money is actually spent. A building that is training a unit still counts, because
  that unit has nothing to do with the upgrade the reserve protects.

## 1.7.0 - 2026-09-19

- New `[auto_production]` (off by default, Ctrl+F10 in game): every idle building of yours trains by itself, all of
  them at once. Halls, keeps and castles make workers up to 6 per hall tier; barracks, aviaries / roosts, mage towers
  / temples and shipyards make the army.
- The mix is dynamic: the map decides the ship share (water fraction plus 5% per oil source, capped by `navy_max`),
  the hall tier decides the land mix, a unit line with more upgrades gets a bigger share, and affordability bends the
  rest, so plenty of gold with little lumber keeps the cheap units coming.
- It always leaves food free (`food_free_min` 4 or `food_free_percent` 10%, whichever is more, counted after the unit),
  keeps back what your next upgrade costs (`reserve_extra`), and only spends when the bank holds `bank_multiple` (4)
  times a unit's current price.
- It respects a mission's unit restrictions, never touches the computer's buildings or a building you have selected,
  never starts a research, and never builds transports, flying machines / zeppelins, dwarves / sappers, heroes or a
  second oil tanker.

## 1.6.2 - 2026-09-19

- `[eye_of_kilrogg] max_active` now defaults to 3 (was 1): up to three eyes out at once across all your ogre-magi.
  An existing config keeps the number it has.

## 1.6.1 - 2026-09-19

- Fixed: Raise Dead autocast never fired. The game keeps corpses out of its unit map, where the mod was looking (the
  computer's own Raise Dead has the same blind spot). The mod now finds every fresh corpse within 15 tiles, also one
  with a living unit standing on it, and skips wrecks on water.
- Raise Dead has to be researched (Temple of the Damned) unless the map grants it; every new map starts with only
  Fireball and Death Coil known. With `[general] log_casts` on, each death knight logs at most every 30 s why it did
  not raise the dead (not researched, mana, no corpse in reach, all claimed).

## 1.6.0 - 2026-09-19

- New `[spell_damage]`: `all` multiplies the damage of every damage spell and the healing of Heal; per-spell numbers
  (`fireball`, `flame_shield`, `blizzard`, `death_and_decay`, `whirlwind`, `death_coil`, `runes`, and `heal` = the most
  HP one Heal restores) replace the game's own first. -1 = the game's value.
- New `[spell_cost]`: `all` multiplies every spell's mana cost, rounded up; one number per spell (all 18) replaces the
  game's cost first. 1 to 255. Heal and Exorcism cost mana per hit point, and the damage multiplier lowers that price.
- New `[mana] regen`: casters regain mana faster (2.0 = twice as fast). Mana stops at 255, that is part of the game.
- Everything is global (the computer's casters get the same numbers) and the game's own values come back in
  multiplayer. The config file gains part 10, "SPELL POWER AND MANA"; "YOUR OWN NUMBERS" is part 11 now.

## 1.5.0 - 2026-09-19

- Autocast for every remaining spell, each its own switch under `[spells]`, all off by default: `fireball`,
  `blizzard`, `death_and_decay`, `whirlwind`, `flame_shield`, `runes`, `invisibility`, `holy_vision`. Area spells hurt
  your own units and buildings in the game, so they are only cast with no friendly unit, flyer, building or wall in the
  blast area (for Fireball: along its whole burning path).
- A Blizzard or Death and Decay the mod started is stopped when a friendly walks in, when no enemy is left, or below
  `[autocast] channel_mana_reserve` mana. New `[autocast] area_min_enemies` (3) and `fireball_min_enemies` (2).
- **Changed:** Raise Dead (on by default) now works like the computer's: it raises any fresh corpse within 15 tiles,
  with or without an enemy nearby, instead of only during a fight within `search_radius`.

## 1.4.1 - 2026-09-19

- Fixed a crash with `[workers] auto_harvest` on: a worker standing idle on the outer edge of the map could be sent to
  harvest a "tree" one tile outside the map, and the game crashed reading past its unit grid. Tiles outside the map no
  longer count as forest, and no order to a position outside the map can reach the game any more, from any feature.
- The tree regrowth log no longer credits trees that grew in the previous game to a freshly (re)started map.

## 1.4.0 - 2026-09-19

- New `[unit_regen]` (off by default): every unit regenerates `hp_per_second` (1) hit points per second of play: land
  units, flyers and ships, never a structure. `regen_for = "all"` or `"mine"`.
- `[heroes]` gains a `regen` switch (off by default) and its amount now defaults to 2. A hero follows `[heroes]` while
  that switch is on and is an ordinary unit otherwise; the two rates never add up. A config from an older version
  with `regen_hp_per_second` above 0 keeps regenerating.

## 1.3.0 - 2026-09-19

- New, EXPERIMENTAL, off by default: `[trees] regrow`. Felled forest grows back, so lumber never runs out for good.
  Every stump waits its own time between `regrow_min_minutes` (10) and `regrow_max_minutes` (20) of play. A tree never
  grows back within `building_distance` (3) tiles of a building or wall or `unit_distance` (3) tiles of a ground unit,
  never where it would close a passage or wall a unit in, and only where a forest stood before. Flyers do not hold it
  up. Regrown trees are ordinary trees: they are saved with the map and the save also loads without the mod.

## 1.2.0 - 2026-09-19

- New `[food] hall_food` (off by default) and `hall_food_amount` (5): every Town Hall / Great Hall, Keep / Stronghold
  and Castle / Fortress gives that much food instead of the game's 1, so a custom game that starts with one worker can
  train from the hall before the first farm. Works at once, also in a running game, for the computer too; the 200
  food limit stays.

## 1.1.0 - 2026-09-19

- New `[gold_mines] amount` and `[oil_platforms] amount` (1.0 by default): multiply the gold in every mine and the oil
  in every patch and platform when a new map starts. Both sides get it, "Gold left" shows the real number, savegames
  keep what they had. Cap 6,553,500 per mine (the map editor stops at 637,500, so up to 10.0 always fits).

## 1.0.10 - 2026-09-18

- The mod is open source under the MIT license (replaces the restrictive license text of 1.0.9). Readme and Nexus
  description updated. No change to the mod's behaviour.

## 1.0.9 - 2026-09-18

- The repository is public: https://github.com/Ensrick/war2r-gameplay-options, with the issue tracker for bug reports.
- `LICENSE`: source-available under the same conditions as the Nexus Mods page (personal use, no re-uploads, no
  conversions, no reuse in other mods, ask before releasing a modified version, credit Ensrick). Readme and Nexus
  description say so and point bug reports at GitHub issues. No change to the mod's behaviour.

## 1.0.8 - 2026-09-18

- `[health] all` now covers EVERYTHING (units, ships and structures), exactly like `[costs] all` and `[time] all`.
  New `[health] units` (and `[health.human] units` / `[health.orc] units`) is the units-only master.
- **Changed meaning:** up to 1.0.7 `[health] all` meant units only. If you had set it, move the number to
  `[health] units` to keep your buildings as they were. `tools/migrate_config.py` (GitHub) does it in place.

## 1.0.7 - 2026-09-18

- Fixed: autocast never saw flying units. The game keeps flyers in a separate air layer that the mod did not scan, so
  no Bloodlust, Haste or Heal went onto your dragons and gryphon riders, no Death Coil, Polymorph, Slow or Exorcism
  onto enemy flyers, and an enemy flyer did not count as "enemy nearby". Both layers are scanned now.

## 1.0.6 - 2026-09-18

- New `[oil_platforms] unlimited` (off by default): oil platforms never run dry, the computer's too. Independent of
  `[gold_mines] unlimited`, which never covered oil.

## 1.0.5 - 2026-09-18

- `[building.watch_tower]` and `[building.orc_watch_tower]` are accepted for the orc scout tower (the game calls it
  Watch Tower), `[building.scout_tower]` for the human one.

## 1.0.4 - 2026-09-18

- Readme and Nexus description: what to check when nothing happens and no `gameplay_options.log` appears (the game
  that was started is not the install the mod is in). No change to the mod's behaviour.

## 1.0.3 - 2026-09-18

- "All structures" multipliers: a `structures` key at the top of `[health]`, `[costs]` and `[time]` (both races) and
  under each race table (umbrella over `buildings` and `building_upgrades`). `[costs]` and `[time]` also gain top-level
  `units` and `research` masters. All 1.0 by default; in `[health]` the `all` values stay unit-only.
- The shipped `gameplay_options.toml` is reorganised into ten numbered parts with a banner each and a contents list at
  the top; every race table has UNITS / STRUCTURES / RESEARCH sub-headers.
- Neutral structures (gold mine, dark portal, runestone) are never scaled.

## 1.0.2 - 2026-09-18

- Quieter defaults, on the author's call that players should not have to switch features off: only Heal, Slow,
  Bloodlust and Raise Dead autocast and worker auto-repair are on out of the box. Exorcism, Polymorph, Death Coil,
  Haste, Eye of Kilrogg (cast and auto-scout), worker auto-harvest and hero regeneration default to off.
- The unit examples in the shipped config (dragon / gryphon rider sight 8, the destroyer) are comments.
- An existing `gameplay_options.toml` is never overwritten, so an updating player keeps their behaviour.

## 1.0.1 - 2026-09-18

- `[building.<name>]` tables: the same ten base stats as `[unit.<name>]` for every structure (hit points, armor,
  basic and piercing damage, range, sight, gold, lumber, oil, build time). Towers moved here from the unit names.
- `[health.human]` / `[health.orc]` gain `buildings` and `building_upgrades`. Structure health follows only these
  two keys; the `all` masters and the unit groups stay unit-only.
- Unit price multipliers now scale oil as well as gold and lumber.
- The `[heroes] units` list is grouped by side, and unit names accept short and in-game spellings
  (`uther`, `grom`, `grommash_hellscream`, `teron`, `korgath`, `gryphon` ...).

## 1.0.0 - 2026-09-18 - Initial release

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
