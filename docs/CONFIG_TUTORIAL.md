# Config tutorial: gameplay_options.toml

All settings live in one text file: `Warcraft II Remastered\x86\gameplay_options.toml`. Open it with Notepad.

## What is on when you install it

Only four autocasts (**Heal, Slow, Bloodlust, Raise Dead**) and **worker auto-repair**. Every other feature is off, every
multiplier is 1.0, and the unit / building examples in the file are comments. Nothing rebalances your game until you
turn it on.

## The three rules

1. **Save while playing.** The mod re-reads the file every couple of seconds. A banner in game says
   "Gameplay Options: settings reloaded". No restart needed.
2. **Mistakes are safe.** If the file has a typo the mod keeps your previous settings, shows
   "gameplay_options.toml has an error" in game, and writes the line number to `x86\gameplay_options.log`. A misspelled setting name
   is reported in the log as "unknown key" instead of being silently ignored.
3. **Delete to reset.** Delete `gameplay_options.toml` and the default file is written again the next time a game starts.

## How the file is written (TOML in one minute)

```toml
# A line starting with # is a comment. The mod ignores it.

[spells]            # a section name in square brackets
heal = true         # on / off settings are true or false (lower case)
                    
[heal]
min_missing_hp = 10 # numbers are written plain

[general]
toggle_key = "F9"   # text goes in double quotes

[polymorph]
targets = ["dragon", "daemon"]   # a list: square brackets, items in quotes, commas between
```

A list may run over several lines, and a comma after the last item is fine.

## Recipes

### Turn spells on or off

```toml
[spells]
exorcism = true      # off by default
polymorph = true     # off by default
death_coil = true    # off by default
haste = true         # off by default
slow = false         # on by default
```

Fireball, Flame Shield, Invisibility, Blizzard, Holy Vision, Death and Decay, Whirlwind and Runes have switches too
(`fireball`, `flame_shield`, `invisibility`, `blizzard`, `holy_vision`, `death_and_decay`, `whirlwind`, `runes`), all
off. Read the next two sections before you turn one on.

### Fireball, Blizzard, Death and Decay, Whirlwind, Runes, Flame Shield: read this first

These six spells hurt **everyone** in their area: your own units, your allies' units, flyers and buildings, just like
when you cast them yourself. The game has no protection against friendly fire. So the mod only casts them where no
unit, flyer or building of yours or of an ally, and no wall, is close ("within N tiles" = N tiles in any direction,
diagonals included):

| Spell | Cast at | Nothing friendly within |
|---|---|---|
| fireball | an enemy, when its burning line (the target tile and about 7 tiles behind it, away from the mage) hits at least `fireball_min_enemies` enemies | 2 tiles of that line (walls: 1) |
| blizzard, death_and_decay | the most valuable spot within 2 tiles (buildings count `area_building_value` each, units 1), with at least one enemy building or `area_min_enemies` enemy units in it, and only with mana for 3 waves | 4 tiles (walls: 3) |
| whirlwind | the same kind of group; one whirlwind per death knight at a time | 6 tiles, because it wanders |
| runes | 2 or more enemy ground units, not within 2 tiles of runes already on the ground | 6 tiles, **the ogre-mage included**: a rune hurts whoever steps on it, whoever owns it |
| flame_shield | one of your melee units in a fight with 2 or more enemies close | 3 tiles, **the mage included**; only the shielded unit is safe |

The mage or death knight that casts a Fireball, Blizzard, Death and Decay or Whirlwind is never hurt by it, so it may
stand close. A caster looks for a target within `search_radius` and never further than the spell's own range.

**Buildings are the better target.** A building cannot walk out of a blizzard, so each enemy building in the blast
counts `area_building_value` (3 by default) against one for each enemy unit, and the spot worth the most wins: two
buildings and two units beat four units. One building is reason enough to cast; without a building it takes
`area_min_enemies` units, so a building and a unit are a valid target while two lone units are not. A building is
aimed at the middle of its footprint, so a 4x4 castle takes the wave on its centre and not on one corner.

**The aim walks around your own units.** The waves do not all land on the aim tile: death and decay scatters its
clouds 2 tiles either side of it, blizzard its chains from 1.5 tiles before to 2.5 tiles past it. So when your own
army is in contact with the enemy the mod does not give the cast up, it moves the aim to the far side of the target
and lets the scatter do the rest: the building or the group is still hit, and nothing of yours is within
`area_friendly_clearance` tiles of the aim. Only when no such spot is left is the spell held back.

```toml
[autocast]
area_friendly_clearance = 4   # tiles around the aim tile that must hold nothing of yours (0 to 6, default 4)
```

| Value | What it means |
|---|---|
| 4 (default) | Nothing of yours can be touched. The furthest impact is 2 tiles (64 px) from the aim and its splash reaches 42 px, so 106 px in all: a unit 4 tiles away (128 px) is out of reach. |
| 3 | 96 px against those 106 px: a unit exactly 3 tiles away can catch a quarter hit (about a sixth of the damage) from the outermost cloud. Rare, and it buys a lot more casts. |
| 0 | Your own units are ignored, the way the computer plays it. Expect losses. |

The watchdog that stops a running channel uses the same number, so raising it also makes a channel end sooner when
one of your units walks in. Walls keep their own 3 tiles.

**No overkill on buildings.** One wave takes roughly 5 x the spell's damage number off a structure it is aimed at
(about 50 hit points with the game's 10, more if you raised it in `[spell_damage]`), so the mod does not start a
channel on buildings a single wave would already flatten, and it stops one as soon as the waves it has paid for cover
the hit points those buildings had. Units are left out of that sum: they move.

What the mod cannot foresee: one of your units walking into the area later. Blizzard and Death and Decay keep going,
wave after wave, until the caster runs out of mana. So the mod watches every Blizzard and Death and Decay it started and
stops it as soon as a friendly unit or building comes within 4 tiles, when no enemy is left within 3 tiles, when the
buildings it was aimed at are covered by the waves already cast, or when the caster's mana drops below
`channel_mana_reserve`. This keeps working while autocasting is switched off with the hotkey.
A Blizzard you cast yourself is never touched. A Flame Shield stays on its unit for a while, a Whirlwind wanders and
runes stay on the ground for 2048 game steps: keep your own units clear of them.

```toml
[spells]
blizzard = true
death_and_decay = true

[autocast]
area_min_enemies = 4          # units needed when there is no building in the blast (default 3)
area_building_value = 5       # an enemy building is worth this many units when the spot is picked (default 3)
fireball_min_enemies = 1      # fireball a single enemy too, as the computer does (default 2)
channel_mana_reserve = 50     # stop a Blizzard / Death and Decay while 50 mana are left (default 0 = until empty)
```

### Holy Vision and Invisibility

`holy_vision`: an idle paladin at full mana reveals the part of the map with the most ground you have never explored.
It may move your camera to that spot every time it is cast (not tested in game yet). Leave it off if that gets in the
way.

`invisibility`: a mage makes one of your hurt casters or ranged units (half health or less) invisible while you pull it
back with a move order and an enemy is close. Never on a unit that is invisible already. Any attack or spell of the
invisible unit ends it.

### Paladins heal sooner, or later

Heal waits until a unit has lost at least this many hit points. Default 10.

```toml
[heal]
min_missing_hp = 25     # only bother with real wounds
```

Want "only heal units below half health" on top of that?

```toml
[heal]
min_missing_hp = 10
below_percent = 50
```

### Choose what gets polymorphed

`targets` is the complete rule. A unit type that is not in the list is never polymorphed. Order matters: if a
dragon and a knight are both in range, the one that comes first in your list is sheeped. Between two units of the same
type the nearest wins.

```toml
[polymorph]
targets = ["dragon", "gryphon_rider", "daemon", "death_knight", "mage"]   # air and casters only
```

Save the 200 mana for dragons alone:

```toml
[polymorph]
targets = ["dragon", "deathwing"]
```

Unit names you can use:

| Alliance | Horde | Other |
|---|---|---|
| peasant, footman, archer, ranger, knight, paladin, mage, dwarves, gryphon_rider | peon, grunt, axethrower, berserker, ogre, ogre_mage, death_knight, goblin_sappers, dragon | skeleton, daemon, critter |
| alleria, kurdran, khadgar, turalyon, danath, lothar, uther_lightbringer | teron_gorefiend, dentarg, grom_hellscream, korgath_bladefist, chogall, guldan, zuljin, deathwing | |

Only living units can be polymorphed, so machines and ships are ignored even if you list them.

### Haste on every fighter, not just flyers

```toml
[haste]
flyers_only = false
```

### Let casters help allies too

```toml
[autocast]
own_units_only = false
```

### Casters wander too far / not far enough

`search_radius` is how many tiles away a caster looks for a target. It walks into casting range when needed.

```toml
[autocast]
search_radius = 5
```

Raise Dead is the exception: it works exactly like the computer's death knights. A death knight raises any corpse up
to 15 tiles away in any direction, whatever `search_radius` says, and it does not wait for an enemy to show up. Raise
Dead has to be researched first (Temple of the Damned), unless the map grants it; the spell raises every corpse within
about 6 tiles of the one it is aimed at. Mages, death knights, machines, flyers, skeletons and daemons leave no corpse,
and the wreck of a sunk ship is never a target.

### Keep casters swinging instead of casting mid-fight

```toml
[autocast]
cast_while_attacking = false    # casters only cast while idle or guarding
```

### Regeneration: heroes, and every other unit

Both are off by default and have their own switch and number.

```toml
[heroes]
regen = true
regen_hp_per_second = 2     # every hero regains 2 hit points per second
regen_for = "mine"          # your heroes only; "all" includes enemy heroes

[unit_regen]
enabled = true
hp_per_second = 1           # every unit: land, air and ships. Never a structure
regen_for = "all"           # the enemy's units too; "mine" = only yours
```

A hurt unit is then worth keeping instead of being a waste of food. A hero follows `[heroes]` while that switch is on
and is an ordinary unit otherwise; the two numbers never add up. The `units` list under `[heroes]` decides which unit
types count as heroes. A config from an older version that had `regen_hp_per_second` above 0 keeps regenerating.

### Let the Eye of Kilrogg scout for you (or not)

Both switches are off by default.

```toml
[eye_of_kilrogg]
cast = true          # idle ogre-magi cast it by themselves
cast_at_mana = 255   # only at full mana, so Bloodlust always comes first. Lower it for more eyes.
max_active = 3       # eyes out at once, all your ogre-magi together
auto_scout = true    # the eye flies to unexplored ground; an eye you move yourself is left to you
```

### Idle workers

A worker that is stopped with nothing queued counts as idle. Stand Ground never counts as idle.

```toml
[workers]
auto_repair = true       # on by default
repair_idle_seconds = 1
repair_radius = 10
auto_harvest = true      # off by default
harvest_idle_seconds = 10
harvest_radius = 5       # raise this if your idle workers stand far from the trees
```

### Forests that grow back

Off by default. A felled tree grows back after some minutes of play time, so lumber never runs out for good. Every
stump draws its own wait between `regrow_min_minutes` and `regrow_max_minutes`, so a patch that was felled together
fills back in bit by bit instead of all at once. It covers the whole map, the computer's forests too.

```toml
[trees]
regrow = true
regrow_min_minutes = 10  # 1 to 600
regrow_max_minutes = 20  # 1 to 600; the same number as the minimum = a fixed wait
building_distance = 3    # 0 to 10: no tree grows back this close to a building or a wall
unit_distance = 3        # 0 to 10: nor this close to a ground unit of any player (0 = only the unit's own tile)
```

Both times can be changed while a game runs; stumps that are already waiting follow the new numbers. Flyers do not
hold regrowth up: they cross forest anyway. A stump whose wait is over but that is blocked grows back as soon as
nothing is in the way.

What it will never do:

- grow a tree within `building_distance` tiles of any building or wall, within `unit_distance` tiles of a ground unit,
  on a corpse, or on a tile the computer keeps clear for its workers;
- close a passage: a path chopped through a forest stays open, a clearing can shrink to a path one tile wide but no
  further, and no unit can get walled in;
- grow anywhere a forest did not stand before. Forests return to their old outline at most.

The wait is counted from the moment the mod first sees the stump and is not stored in savegames: after loading a game
every stump on the map starts its wait over. The trees themselves are ordinary trees and are saved with the map, so a
save made with regrown forest also loads without the mod.

### Gold mines that never run dry

Off by default. It covers every mine on the map, the computer's too.

```toml
[gold_mines]
unlimited = true
```

### More gold and oil on the map, without making it endless

`amount` multiplies what every gold mine (and every oil patch and platform) holds **when a new map starts**. Both
sides get it, "Gold left" shows the real number, and a savegame keeps what it had. Good for long games with raised
health, where an ordinary mine runs dry before the fight is decided, and it keeps the computer from being starved out
quite so easily.

```toml
[gold_mines]
amount = 3.0        # a 40,000 mine starts at 120,000

[oil_platforms]
amount = 2.0
```

A mine can hold up to 6,553,500. The map editor stops at 637,500, so anything up to 10.0 always fits; beyond that the
value is capped. `unlimited` and `amount` are independent.

### Halls that feed your first units

In the game a Town Hall or Great Hall gives 1 food, exactly enough for the peasant that built it, so in a custom game
that starts with one worker nothing can be trained until a farm stands. With `hall_food` on, every hall (and keep,
stronghold, castle, fortress) gives `hall_food_amount` instead:

```toml
[food]
hall_food = true
hall_food_amount = 5     # 1 = the game's own value; a farm gives 4; the 200 food limit stays
```

Works right away, also in a game that is already running, and for the computer players too. Switch it off and the
game's own numbers come back at once.

### Oil platforms that never run dry

The same thing for oil, with its own switch. Off by default; it covers every platform on the map, the computer's too.
A platform keeps the amount it had when you switched this on (never less than 5000), and an oil patch gets that
amount back when its platform is destroyed.

```toml
[oil_platforms]
unlimited = true
```

### Health, prices, build times: the multipliers

`[health]`, `[costs]` and `[time]` change the game's unit, structure and research data. They are read **when a new map
starts** (new mission, custom game, restart), not in the middle of a game, and a savegame keeps the numbers it was made
with. The computer plays by the same numbers.

All three share one layout, and the values **multiply into each other from the top down**:

```
[costs] all  x  [costs] units / structures / research  x  [costs.human] all  x  [costs.human] units / structures / research  x  the group
```

The file is laid out in numbered parts with a banner each (`6. HEALTH`, `7. PRICES`, `8. BUILD AND RESEARCH TIME`), and
inside every race table the keys sit under `--- HUMAN UNITS ---`, `--- HUMAN STRUCTURES ---` and `--- HUMAN RESEARCH ---`
sub-headers, so the structure settings are the block right under the unit groups.

`1.0` is the game's own number and the default everywhere. `0.5` halves, `2.0` doubles.

```toml
[health]
all = 2.0              # master: EVERYTHING has double health: units, ships and buildings

[health.orc]
all = 1.5              # ...and everything orcish another 50% on top (3x in total)
melee = 1.2            # ...and grunts, ogres and ogre-magi 20% on top of that

[health.human]
heroes = 4.0           # human heroes: 2.0 x 4.0 = 8x

[costs]
all = 1.0

[costs.human]
units = 0.5            # umbrella: every human unit half price
naval_upgrades = 0.5   # ship cannons and ship armor research
buildings = 1.5

[costs.orc]
research = 0.5         # umbrella: all orc research half price
melee_upgrades = 0.5   # battle axes and shields: 0.5 x 0.5 = a quarter

[time]
all = 0.5              # everything trains, builds and researches twice as fast
```

Groups you can use in `[x.human]` and `[x.orc]`:

| Key | Covers |
|---|---|
| `all` | everything of that race: units, ships, structures, research |
| `units` | umbrella over all unit groups, ships included |
| `structures` | umbrella over `buildings` and `building_upgrades` |
| `research` | umbrella over all research groups (`[costs]` and `[time]`) |
| `workers` `melee` `ranged` `siege` `casters` `air` `naval` `demolition` | peasant / peon; footman, knight, paladin / grunt, ogre, ogre-mage; archer, ranger / axethrower, berserker; ballista / catapult; mage / death knight; gryphon rider, flying machine / dragon, zeppelin; every ship; dwarves / sappers |
| `heroes` | the unit types listed under `[heroes] units` (`[health]` only) |
| `buildings` | structures your workers place |
| `building_upgrades` | keep / stronghold, castle / fortress, guard tower, cannon tower |
| `melee_upgrades` `ranged_upgrades` `siege_upgrades` `naval_upgrades` | swords, axes, shields; the elf / troll line; ballista / catapult; ship cannons and armor |
| `paladin_upgrades` (human) / `ogre_mage_upgrades` (orc) | the upgrade itself plus holy vision, healing, exorcism / eye of kilrogg, bloodlust, runes |
| `mage_spells` (human) / `death_knight_spells` (orc) | their spell research |

All three sections start with the same masters, for both races at once:

| Key at the top of `[health]`, `[costs]`, `[time]` | Covers |
|---|---|
| `all` | **everything**: every unit, ship and structure (and all research in `[costs]` / `[time]`) |
| `units` | every unit and ship, never a structure |
| `structures` | every structure, never a unit |
| `research` | all research (`[costs]` and `[time]` only) |

So the one-line "slow the whole game down" settings are `[health] all`, `[costs] all` and `[time] all`. Want tougher
armies but normal buildings? Use `[health] units` instead of `all`. The same keys exist under `[x.human]` / `[x.orc]` for
one race.

`[health.neutral] all` covers skeletons, daemons, critters and the Eye of Kilrogg; the gold mine, dark portal and
runestone are never scaled. Prices scale gold, lumber and oil alike, for units, structures and research.

> Updating from 1.0.7 or older: `[health] all` used to mean "units only". If you had set it, move that number to
> `[health] units` to keep your buildings as they were (`tools/migrate_config.py` in the GitHub repo does it for you).

```toml
[health]
units = 2.0             # every unit and ship, no structure
structures = 1.5        # every structure of both races

[health.human]
structures = 1.2        # human structures another 20% on top
building_upgrades = 2.0 # keep, castle, guard tower, cannon tower on top of that

[costs]
structures = 0.5        # every building and building upgrade half price
```

The engine's own limits cap the results:

| What | Limit | Why |
|---|---|---|
| Unit hit points | 65535 | stored in 16 bits. From 10000 up the status panel stops printing the numbers; the health bar still works |
| Structure hit points | 32767 | the construction progress maths is signed 16-bit |
| Unit or structure price | 2550, in steps of 10 | stored as one byte of tens |
| Research price | 65535 | stored in 16 bits |
| Build / research time | 255 | stored as one byte. Many are already 200-255 (town hall 255, dragon 250, most research 250), so times can be cut freely but barely lengthened |

Any multiplier from 0.01 to 1000 is accepted. What is free stays free; what costs something never becomes free.

### Your own numbers for one unit

One `[unit.<name>]` table per unit. `-1`, or simply leaving a line out, keeps the game's own number. `0` is a real value
where it makes sense (armor, damage, lumber, oil). These are **base** numbers: they replace the game's values first, and
the multipliers above then apply on top of them.

```toml
[unit.elven_destroyer]
hit_points = 105        # 1-65535      game: 100
armor = 11              # 0-255        game: 10
basic_damage = 37       # 0-255        game: 35
piercing_damage = 2     # 0-255        game: 0
range = 5               # 0-20 tiles   game: 4
sight = 9               # 0-9          game: 8
gold = 600              # 0-2550, steps of 10   game: 700
lumber = 300            #                       game: 350
oil = 500               #                       game: 700
build_time = 80         # 0-255        game: 90
```

The shipped config contains this destroyer example, and `sight = 8` for dragons and gryphon riders (the game's value is 6),
as **comments**. Remove the `# ` in front of the lines to switch one on.

Names: every unit name from the Polymorph list above, plus `ballista`, `catapult`, `flying_machine`, `zeppelin` and the
ships (`human_tanker`, `orc_tanker`, `human_transport`, `orc_transport`, `elven_destroyer`, `troll_destroyer`,
`battleship`, `juggernaught`, `gnomish_submarine`, `giant_turtle`). Short names work too (`uther`, `grom`, `gryphon`).

### Your own numbers for one building

Exactly the same, in `[building.<name>]` tables. The damage and range keys matter for the towers; `hit_points` tops out at
32767 for structures.

```toml
[building.human_guard_tower]
hit_points = 200        # game: 130
piercing_damage = 14    # game: 12 (plus 4 basic)
range = 7               # game: 6

[building.farm]
gold = 400              # game: 500
lumber = 200            # game: 250
build_time = 80         # game: 100
```

Names: `farm` `pig_farm` `human_barracks` `orc_barracks` `church` `altar_of_storms` `human_scout_tower` `orc_scout_tower`
`stables` `ogre_mound` `gnomish_inventor` `goblin_alchemist` `gryphon_aviary` `dragon_roost` `human_shipyard` `orc_shipyard`
`town_hall` `great_hall` `elven_lumber_mill` `troll_lumber_mill` `human_foundry` `orc_foundry` `mage_tower`
`temple_of_the_damned` `human_blacksmith` `orc_blacksmith` `human_refinery` `orc_refinery` `human_oil_platform`
`orc_oil_platform` `keep` `stronghold` `castle` `fortress` `human_guard_tower` `orc_guard_tower` `human_cannon_tower`
`orc_cannon_tower` `human_wall` `orc_wall` `gold_mine` `dark_portal` `runestone`.

The orc "Watch Tower" of the game is `orc_scout_tower`; `watch_tower` and `orc_watch_tower` are accepted as well, and
`scout_tower` means the human one. Guard and cannon towers need the `human_` / `orc_` prefix.

A unit name under `[building.*]` (or the other way round) is refused with a note in the log, never misapplied.

### Longbow and Lighter Axes

```toml
[range]
upgrade_bonus = 2     # rangers and berserkers gain +2 range from their upgrade. The game's own bonus is 1
```

One number for both upgrades: the game uses a single rule for the two. This one applies right away, no new map needed.

### Spell costs, spell damage and mana

```toml
[spell_cost]
all = 1.0           # x every mana cost, rounded UP to a whole number
fireball = 50       # your own cost for one spell (game: 100), the multiplier goes on top
heal = 3            # heal and exorcism cost mana PER HIT POINT (game: 5 and 4)

[spell_damage]
all = 2.0           # x every damage number below, and x the heal limit per cast
runes = 80          # your own number for one spell (game: 50), the multiplier goes on top

[mana]
regen = 2.0         # casters regain mana twice as fast (the game: 1 point every 40 game steps)
```

Every key is optional; `-1` or a missing key means the game's own number. These three sections apply right away, in the
running game too (no new map needed), and switch themselves off in multiplayer.

**Costs.** One key per spell: `holy_vision` `heal` `exorcism` `flame_shield` `fireball` `slow` `invisibility`
`polymorph` `blizzard` `eye_of_kilrogg` `bloodlust` `raise_dead` `death_coil` `whirlwind` `haste` `unholy_armor` `runes`
`death_and_decay`. A cost is 1 to 255: mana never goes above 255, so a dearer spell could never be cast, and a free heal
or exorcism would crash the game. Blizzard and Death and Decay charge their cost for every wave while you channel them;
Raise Dead charges per skeleton.

**Damage.** The number is what one hit does (the game then rolls between half and all of it for the area spells):

| Key | The game | Most | What it is |
|---|---|---|---|
| fireball | 40 | 254 | each of the fireball's 5 blasts |
| flame_shield | 4 | 254 | each pulse of each of the 5 flames |
| blizzard | 10 | 254 | each falling shard (a wave drops 5 x 11) |
| death_and_decay | 10 | 254 | each pulse (a wave makes 5 clouds of 10 pulses) |
| whirlwind | 4 | 254 | each of its 400 pulses |
| death_coil | 50 | 127 | the total, shared out over the enemies hit; the caster heals by what it deals |
| runes | 50 | 128 | each rune; any unit that steps on one, yours included |
| heal | 40 | 255 | the most hit points one Heal restores |

Heal and exorcism have no damage number of their own: they cost mana per hit point, so `[spell_damage] all` makes them
**cheaper per hit point** instead (their cost is divided by it, rounded up, never below 1), and it raises the heal limit
per cast. Example: `[spell_cost] heal = 3` with `[spell_damage] all = 2` heals 1 hit point per 2 mana, up to 80 per cast.
That also means the most it can do is x5 for heal and x4 for exorcism (1 mana per hit point).

The values are clamped to what the game can hold and the log says so; the log also lists every changed cost and damage
number when a map starts.

**Mana.** `[mana] regen` from 0.1 to 40 speeds up (or slows down) how fast every caster refills. The limit of 255 mana
per caster is built into the game and cannot be raised.

Good to know:

- **These numbers are global.** The computer's casters pay the same costs and deal the same damage as yours.
- The computer only casts Blizzard and Death and Decay with three times their cost in mana, so with a cost above 85 it
  stops casting them.
- The spell descriptions in the game's tooltips are fixed text ("Deals 34 hit point damage", "1 hit point per 5 mana")
  and do not change; the mana cost shown in the tooltip does.

### Buildings that train by themselves

```toml
[auto_production]
enabled = true          # Ctrl+F10 also turns it on and off in game
```

Every **idle** building of yours then trains on its own, all of them in the same moment: town halls, keeps and castles
make workers, barracks, aviaries / roosts, mage towers / temples and shipyards make the army. The computer's buildings
are never touched, a building you have selected is left alone (so your own click is never stolen), and the mod never
starts a research or a building upgrade.

What it builds: **workers, footmen / grunts, archers / axethrowers (rangers / berserkers once upgraded), knights /
ogres (paladins / ogre-mages once upgraded), mages / death knights, gryphon riders / dragons, ballistas / catapults,
destroyers, battleships / juggernaughts, submarines / turtles when you are rich, and one oil tanker once you own an
oil platform**. It never builds transports, flying machines / zeppelins, dwarves / sappers or heroes, and it never
builds a second tanker: ferrying, scouting and oil runs stay yours.

```toml
[auto_production]
workers_tier1 = 12          # workers wanted with a town hall / great hall
workers_tier2 = 16          # with a keep / stronghold
workers_tier3 = 24          # with a castle / fortress (0 = never train workers)
food_free_min = 4           # always leave this much food free: 4, or
food_free_percent = 10      # 10 % of your supply, whichever is more
reserve_extra = 0.25        # money kept back for upgrades (below)
upgrade_bias = 0.25         # a line with upgrades gets a bigger share (below)
filler_min = 10             # spare money for this many units unblocks the filler rule (below)
plenty_units = 10           # able to buy this many of a unit = rich enough that its price stops bending the mix
save_up_seconds = 60        # how long a building keeps its money for a unit it cannot pay for yet (below)
navy_weight = 1.0           # how much of the map's water turns into ships (0 = never build ships)
navy_max = 80               # ships are never more than this much of the army, in percent
workers_ignore_reserve = true  # workers only wait for their own price, never for the upgrade reserve
tankers_ignore_reserve = true  # and the one tanker with them

[auto_production.units]         # switch a whole class off
flyers = false

[auto_production.bank_multiple] # spare money the bank must hold, as a multiple of the unit's own price
all = 4.0
knights = 8.0                   # be richer before each knight
```

**The food you keep.** Before anything is trained the mod checks the food you would have **after** that unit: at least
`food_free_min`, or `food_free_percent` of your supply, whichever is larger, must still be free. That room is yours,
for peasants, transports, zeppelins or a tanker of your own. Units already in training count as used food (the game's
own "not enough food" message does not count them, which is why it lets you overshoot).

**The upgrade reserve.** The mod adds up every upgrade you could buy right now (weapons, armor, the ranger / berserker
line, spells, the paladin / ogre-mage upgrade, keeps, castles, guard and cannon towers) and keeps the dearest one plus
`reserve_extra` of all the others in the bank. The more you could be researching, the more it saves, so a unit is only
built when it does not eat your next upgrade. **Building upgrades are the exception: a keep, a castle or a tower never
sets that floor, it only ever counts at the `reserve_extra` weight** — a research is money you have already decided to
spend, while a keep is something you buy when you want it, and left to anchor the reserve its 2000 gold would stop
your army for the first ten minutes of every game.

**Workers and the tanker are outside all of that.** They are the economy, not army shopping: they wait for their own
price, the food rule, the mission list and the count target, and for nothing else. Early on the reserve is easily
larger than your whole bank (the keep upgrade alone is 2000 gold), and a mod that stops making peasants there would
never get the economy going. Set `workers_ignore_reserve` / `tankers_ignore_reserve` to false to put them back under
the same rules as the army.

**When a unit is affordable.** Not a fixed sum: everything left after the reserve must pay for `bank_multiple` of that
unit, in every resource it costs. Four grunts' worth of spare gold before a grunt, four battleships' worth of spare
gold, lumber and oil before a battleship. Raise the price of a unit anywhere (`[costs]`, `[unit.knight]`, the map's own
data) and the threshold rises with it. `all` is the master, a class name overrides it for that class, and submarines
always want twice their number.

**The mix numbers are weights.** Only the ratios count: `75 / 20 / 5` and `15 / 4 / 1` build the same army, and a
tier's numbers need not add up to 100. Each class's weight is its number times its upgrade bonus times how affordable
it is right now; land and ships are weighed separately, so raising a ship number never starves your land army.

**When you are rich, prices stop counting: `plenty_units`.** If you could pay for ten of something, being able to pay
for a hundred of it does not make it any more worth building. So a class your spare bank (what is left after the
upgrade reserve) buys `plenty_units` of or more is at its **full** configured share, and the ratios between your
gold, lumber and oil play no part at all: with plenty of everything the army comes out exactly as
`land_tierN` / `navy_tierN` say. Only below that line does money bend the mix, and it bends it in proportion: a class
the bank buys 3 of at `plenty_units = 10` gets three tenths of its share, and the rest goes to the classes you can
still pay for. Lower it (say 5) to let a thin bank matter longer; raise it to make the mod insist on being properly
rich before it treats a class as freely available.

Three settings here count units of bank and are easy to mix up. `plenty_units` is a **weight**: it decides how big a
share of the mix a class gets while the mix is working. `bank_multiple` is a **gate**: it decides whether a unit may
be started at all, and no amount of extra money past it buys anything. `filler_min` is neither; it only comes into it
when the mix has **nothing** a building can afford, as the size of bank that then justifies building off-mix.
Changing one does not change the others.

**Ships come from the map.** On the first pass of a map the mod counts the water tiles and the oil patches and
platforms on it, and writes what it found to the log:

```
production: map is 50 % water with 2 oil source(s): ships get 72 / 60 / 54 % of the army by hall tier
```

The share is the water fraction plus 5 % per oil source (at most 30 % from oil), a little higher while you only have a
town hall and lower with a castle, then `navy_weight` and the `navy_max` cap. A map with no oil and less than 10 %
water is a land map: no ships at all, whatever else happens. `navy_weight = 0` turns ships off everywhere.

**The army composition.** Land and ships have their own percentages per hall tier, and the map's share decides how the
army is split between the two:

```toml
[auto_production.land_tier3]  # with a castle / fortress
archers = 15
knights = 40
casters = 25
flyers = 15
siege = 5

[auto_production.navy_tier3]
destroyers = 25
battleships = 60
submarines = 10
```

These are shares, not hard limits, and they only have to be shares of each other (60 and 20 mean the same as 6 and 2).
Four things bend them:

- **what you can pay for.** A class you cannot afford drops out and its share goes to the others.
- **the filler.** When a building can afford nothing from its mix but the bank is fat (more than `filler_min` of
  something it could train), it builds that instead. This is what keeps a gold-rich, lumber-poor game moving: grunts
  out of a tier-3 barracks, where grunts have no share at all. It never crosses the land / ship line.
- **your upgrades.** Every upgrade level of a class's line adds `upgrade_bias` (25 % by default) to its share, so a
  fully upgraded line is built more often.
- **the hall tier.** A keep switches to `land_tier2` / `navy_tier2`, a castle to the tier-3 tables.

A building whose classes are all well past their share (more than 2 units and more than 25 % over) waits instead, so
the money goes to whatever is behind.

**Saving up for what you cannot pay for yet.** A shipyard can afford a destroyer long before it can afford a
battleship, and both are paid for in the same scarce oil: left alone it buys destroyer after destroyer, the oil never
reaches a battleship's price, and the fleet ends up all destroyers however much of the mix belongs to battleships.
So when the class that is furthest behind its share is held back **by money alone**, the building keeps its money
instead of spending that resource on a cheaper class. Only money counts: a class you may not build yet, one at its
`no_enemy_navy_cap` ceiling and the food rule are other things entirely, and never make a building wait.

**A class that does not cost the blocked resource is built as before**, so a gold-rich, lumber-poor game keeps making
grunts while its knights wait, and no land unit ever waits for oil. And saving never becomes a deadlock:
`save_up_seconds` (60 by default) is how long the building goes on waiting **without the blocked resource growing**.
While the oil is still coming in it waits as long as it takes; once the oil stands still for a minute it gives up,
builds the best thing it can afford, and starts over. `save_up_seconds = 0` turns the whole rule off. With
`log_casts = true` the log says what is happening:

```
production: saving oil for battleship (have 2950, need 4120)
```

**When the enemy has no navy, neither do you.** While no hostile player owns a shipyard or a warship anywhere on the
map, the mod holds at most **1 oil tanker, 5 destroyers, 2 battleships / juggernaughts and 2 submarines** and builds
no further ships, however much water there is; the share that would have gone to ships goes to your land army
instead. Ships you already have and ships in training count, and nothing is ever cancelled. The moment a hostile
shipyard appears anywhere — even half built, even in the fog, and a stray enemy warship counts too — the caps lift
and the normal map-driven navy takes over. Change the ceilings with:

```toml
[auto_production.no_enemy_navy_cap]
tankers = 1
destroyers = 5
battleships = 2
submarines = 2
```

A class you leave out of that table has no ceiling at all (0 to 200 each; 0 means "never build it while the enemy has
no navy").

**Submarines / turtles** are a luxury: they need twice the bank margin of anything else and never take more than a
fifth of your ships, whatever `navy_tierN` says. They are also blind to what they cannot see: the mod does not build
spotters for them, so keep a flying machine or a zeppelin of your own around if you care about enemy submarines.

Good to know:

- Auto-production only runs in single player, like every other feature that gives orders.
- **Mission restrictions are obeyed.** Campaign and custom maps can forbid units, upgrades and keeps or castles, and
  the mod checks every one of them against that list before it builds or reserves money, every pass. A mission where
  you may not have knights gets no knights, their share goes to the rest of your army, and a barracks whose whole
  list is forbidden simply stays idle. The game's own train command does not check this, so the mod has to.
- A new unit stands next to the building that made it. Warcraft II has no rally point, so the mod cannot set one.
- If a unit cannot be placed (no free tile), that building waits 10 seconds before trying again.
- `[general] log_casts = true` writes one line per start to `x86\gameplay_options.log`.
- **Nothing being built?** With `log_casts = true` the log also gets one line every 30 seconds saying why, with the
  numbers behind it:
  `production: nothing (workers 6/6, food free 194, gold 700 lum 20 oil 0, reserve 0/0/0, blocked: workers=enough,
  infantry=bank, archers=lumber, knights=prereq, siege=prereq)`. Per class it names the first thing in the way:
  `off` (switched off), `mission` (the map forbids it), `prereq` (a building or upgrade is missing), `busy` /
  `waiting` (the building is working, or waiting out a failed start), `food`, `gold` / `lumber` / `oil` (the price
  itself), `reserve` (the upgrade reserve), `bank` (affordable, but not `bank_multiple` times over), `no platform`
  (the tanker), `saving` (the building could pay for it, but is keeping that resource for a class further behind its
  share), and `enough` with the mix deficit when there are simply enough of them already.

### Which spell first, and saving mana for it

Every caster tries its spells in an order. `[priority]` is that order, one list per caster, written with the same
names as `[spells]`:

```toml
[priority]
save_mana = true
paladin      = ["heal", "exorcism", "holy_vision"]
mage         = ["polymorph", "slow", "fireball", "invisibility", "blizzard", "flame_shield"]
ogre_mage    = ["bloodlust", "runes"]
death_knight = ["raise_dead", "unholy_armor", "death_coil", "haste", "death_and_decay", "whirlwind"]
```

Those are the defaults: leave the section out and nothing changes. Move a spell to the front and your casters reach
for it first:

```toml
[priority]
death_knight = ["death_and_decay", "raise_dead", "death_coil"]
```

**`save_mana` (on by default) is what makes that stick.** A death knight with 60 mana can pay for a Death Coil but
not for a Death and Decay (a channel asks for three waves up front). Without `save_mana` it coils, is broke again,
and the channel you put first never happens. With `save_mana` it checks the spells above the one it could afford: if
one of them has a proper target right now and only the mana is missing, the knight casts **nothing** and keeps
saving until it can. When that spell has no target, the ones below it go ahead as before. Set `save_mana = false`
and the lists are only an order, never a reason to hold back.

Good to know:

- A spell switched off in `[spells]` is skipped wherever it sits in the list, and is never a reason to save.
- A name that is misspelled or belongs to another caster is written to the log and ignored; the rest of the list
  still works. Spells you leave out are appended at the end in the default order, so nothing is switched off by
  being forgotten.
- Heroes use their caster's list (Teron Gorefiend follows `death_knight`, Khadgar follows `mage`).
- Eye of Kilrogg is not in the lists: it has its own rule in `[eye_of_kilrogg]`, with its own mana threshold.
- With `[general] log_casts = true` a caster that is saving writes one line every 30 seconds:
  `saving: death_knight at 46,33 mana 60 for death_and_decay (needs 90)`.

### Change or remove the hotkey

`Ctrl` plus this key toggles autocast in game.

```toml
[general]
toggle_key = "F7"     # "F1" to "F12"
# toggle_key = ""     # no hotkey
```

### See what the mod is doing

```toml
[general]
log_casts = true
```

Every cast is then written to `x86\gameplay_options.log` with the caster, the target and the map position. A death
knight that could not raise the dead also says why ("not researched", "mana below the cost", "no corpse within 15
tiles", "all corpses ... are claimed", "switched off"), at most once every 30 seconds of play per death knight.

### See what the computer player is doing

```toml
[general]
log_ai = true
```

For working out why a computer player stopped attacking. Once a minute of play, every computer player that still has
units writes one line to `x86\gameplay_options.log`:

```
ai: player 3 script 41 pc 0x3058 WAITFOR have_castle same pc for 12m | gold 3750 lum 1000 oil 4700 | food 24/60 |
force land 13 sea 0 air 0 | foot 6/6 arch 3/3 siege 0/0 knight 4/4 | workers 8/8 | buildlist 9/13
```

The computer runs a small script per player. `WAITFOR` is the one instruction that can block: it re-checks its
condition every step for ever, so a computer that is waiting for something it can no longer get never attacks again
while still gathering and building. The line says which instruction it is on, how long it has been there, and the
numbers that instruction is waiting for, so you can see what is missing (in the example: a castle it cannot start
because a castle costs 1200 lumber and it has 1000). After five minutes on the same instruction there is also a
`ai: player 3 has been on WAITFOR have_castle for 5 min` line, repeated every five minutes.

A script also sleeps on purpose between its steps. The line then reads `sleeping 9000 steps, then WAITFOR ...`, and
sleeping never counts as being stuck: the five minutes only run while the script is awake and waiting.

This setting only reads: it never changes anything, for you or for the computer. Leave it off for normal play, it
makes the log long.

## Every setting

| Section | Setting | Default | Meaning |
|---|---|---|---|
| general | enabled | true | Master switch for autocasting (same as the hotkey) |
| general | toggle_key | "F9" | Ctrl + key toggles autocast |
| general | interval_ticks | 10 | Game steps between autocast passes. Lower reacts faster |
| general | log_casts | false | Write every cast to gameplay_options.log |
| priority | save_mana | true | A caster keeps its mana for a spell higher in its list instead of casting a cheaper one |
| priority | paladin, mage, ogre_mage, death_knight | today's order | The order each caster tries its spells in, by `[spells]` name |
| general | log_ai | false | Write what each computer player's script is waiting for, once a minute (read-only diagnostic) |
| autocast | search_radius | 8 | Tiles a caster searches for targets (Raise Dead always looks 15 tiles around, like the computer) |
| autocast | combat_radius | 6 | A unit counts as fighting when an enemy is this close to it |
| autocast | own_units_only | true | Friendly spells skip allies' units |
| autocast | cast_while_attacking | true | Casters may interrupt their own attack to cast |
| autocast | area_min_enemies | 3 | Enemy units within 2 tiles of the target before Blizzard, Death and Decay or Whirlwind is cast, when no enemy building is in the blast (1 to 50) |
| autocast | area_building_value | 3 | What an enemy building in the blast is worth in units when the spot is picked (1 to 20) |
| autocast | area_friendly_clearance | 4 | Tiles around a Blizzard / Death and Decay aim tile that must hold nothing of yours (0 to 6) |
| autocast | fireball_min_enemies | 2 | Enemies the Fireball's burning line must hit; 1 = the computer's own rule (1 to 50) |
| autocast | channel_mana_reserve | 0 | A Blizzard / Death and Decay the mod started stops below this mana; 0 = until empty (0 to 255) |
| spells | heal, slow, bloodlust, raise_dead | true | One switch per spell |
| spells | exorcism, polymorph, death_coil, haste, unholy_armor | false | |
| spells | holy_vision, flame_shield, fireball, invisibility, blizzard, death_and_decay, whirlwind, runes | false | Friendly fire: see "Fireball, Blizzard, ... read this first" |
| heal | min_missing_hp | 10 | Heal only units missing at least this many HP |
| heal | below_percent | 100 | Also require HP at or below this percent. 100 = off |
| polymorph | targets | flyers, casters, big ground units | Valid targets in priority order |
| haste | flyers_only | true | Haste only your air units |
| heroes | units | the 15 campaign heroes | What counts as a hero |
| heroes | regen / regen_hp_per_second | false / 2 | Heroes regenerate this many hit points per second |
| heroes | regen_for | "all" | "all" or "mine" |
| unit_regen | enabled / hp_per_second / regen_for | false / 1 / "all" | Every unit and ship regenerates (never a structure); heroes follow [heroes] while that is on |
| eye_of_kilrogg | cast | false | Idle ogre-magi cast Eye of Kilrogg |
| eye_of_kilrogg | cast_at_mana | 255 | Mana needed before casting it |
| eye_of_kilrogg | max_active | 3 | Eyes out at the same time, all ogre-magi together |
| eye_of_kilrogg | auto_scout | false | Eyes fly to unexplored ground |
| gold_mines | unlimited | false | Mines never run dry (all mines) |
| gold_mines | amount | 1.0 | Multiplies the gold in every mine when a new map starts (cap 6,553,500 per mine) |
| oil_platforms | unlimited | false | Oil platforms never run dry (all platforms) |
| food | hall_food / hall_food_amount | false / 5 | Halls, keeps and castles give this much food instead of 1 (1 to 200) |
| oil_platforms | amount | 1.0 | Multiplies the oil in every patch and platform when a new map starts |
| workers | auto_repair / repair_idle_seconds / repair_radius | true / 1 / 10 | Idle workers repair your damaged buildings |
| workers | auto_harvest / harvest_idle_seconds / harvest_radius | false / 10 / 5 | Idle workers go to the nearest mine or tree |
| trees | regrow / regrow_min_minutes / regrow_max_minutes | false / 10 / 20 | Felled forest grows back; every stump waits its own time between min and max |
| trees | building_distance / unit_distance | 3 / 3 | No regrowth this close to a building or wall / to a ground unit (flyers do not count) |
| health / costs / time | all | 1.0 | Master multiplier of the section: everything (units, ships, structures, research) |
| health / costs / time | units, structures | 1.0 | Every unit and ship / every structure, both races |
| costs / time | research | 1.0 | All research of both races |
| health.human / health.orc | all, units, the 8 unit groups, heroes; structures, buildings, building_upgrades | 1.0 each | Health by race, kind and group. Units cap 65535, structures 32767 |
| health.neutral | all | 1.0 | Skeletons, daemons, critters, Eye of Kilrogg |
| costs.human / costs.orc | all, units, structures, research, the 8 unit groups, buildings, building_upgrades, the research groups | 1.0 each | Price multipliers, caps 2550 / 65535 |
| time.human / time.orc | same keys as costs | 1.0 each | Training, construction, upgrade and research time, cap 255 |
| range | upgrade_bonus | 1 | Range added by Longbow / Lighter Axes |
| auto_production | enabled / toggle_key | false / "F10" | Idle buildings of yours train by themselves; Ctrl + key toggles it |
| auto_production | workers_tier1 / 2 / 3 | 12 / 16 / 24 | Workers wanted at your best hall tier: hall, keep, castle (0 to 200, 0 = none) |
| auto_production | food_free_min / food_free_percent | 4 / 10 | Food always left free: the larger of the two |
| auto_production | reserve_extra | 0.25 | Bank kept for upgrades: the dearest purchasable one + this much of the rest |
| auto_production | upgrade_bias | 0.25 | Extra share per upgrade level of a class's line |
| auto_production | filler_min | 10 | Spare money for this many units before the filler rule builds off-mix (only when the mix has nothing affordable) |
| auto_production | plenty_units | 10 | Spare money for this many of a class = its full share in the mix; below that its share shrinks in proportion (1 to 100) |
| auto_production | save_up_seconds | 60 | A building keeps its money for the class furthest behind its share instead of spending that resource on a cheaper one; it gives up after this many seconds without the resource growing (0 to 600, 0 = never save up) |
| auto_production | navy_weight | 1.0 | Scales the ship share the map asks for; 0 = never build ships |
| auto_production | navy_max | 80 | Ships never take more than this much of the army, in percent |
| auto_production | workers_ignore_reserve | true | Workers wait for their own price only, never for the upgrade reserve or bank_multiple |
| auto_production | tankers_ignore_reserve | true | The same for the single oil tanker |
| auto_production.units | workers, infantry, archers, knights, casters, flyers, siege, tankers, destroyers, battleships, submarines | true each | Switch a class off |
| auto_production.bank_multiple | all, then the same class names | 4.0 | Spare bank needed, as a multiple of the unit's own price (submarines always want twice) |
| auto_production.no_enemy_navy_cap | tankers, destroyers, battleships, submarines | 1 / 5 / 2 / 2 | Most of a class the mod holds while no hostile player owns a shipyard or a warship (0 to 200; a class left out is uncapped) |
| auto_production.land_tier1 / 2 / 3 | infantry, archers, knights, casters, flyers, siege | see the recipe | Land shares in percent, per hall tier |
| auto_production.navy_tier1 / 2 / 3 | destroyers, battleships, submarines | see the recipe | Ship shares in percent, per hall tier |
| spell_cost | all | 1.0 | Multiplies every spell's mana cost, rounded up, 1 to 255 |
| spell_cost | holy_vision ... death_and_decay (18 spells) | -1 each | Your own mana cost for one spell, -1 = the game's. Heal and exorcism: mana per hit point |
| spell_damage | all | 1.0 | Multiplies every damage number and the heal limit; makes heal and exorcism cheaper per hit point |
| spell_damage | fireball, flame_shield, blizzard, death_and_decay, whirlwind, death_coil, runes, heal | -1 each | Your own damage per hit (heal: hit points per cast), -1 = the game's; limits 254 / 127 (death_coil) / 128 (runes) / 255 (heal) |
| mana | regen | 1.0 | How fast casters regain mana (0.1 to 40). The 255 mana limit stays |
| unit.NAME / building.NAME | hit_points, armor, basic_damage, piercing_damage, range, sight, gold, lumber, oil, build_time | -1 each | Base stats of one unit or structure type, -1 = the game's value |
