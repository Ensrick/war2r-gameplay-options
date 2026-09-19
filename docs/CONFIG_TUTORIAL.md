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

### Keep casters swinging instead of casting mid-fight

```toml
[autocast]
cast_while_attacking = false    # casters only cast while idle or guarding
```

### Hero regeneration

Off by default.

```toml
[heroes]
regen_hp_per_second = 1     # every hero regains 1 hit point per second
regen_for = "mine"          # your heroes only; "all" includes enemy heroes
```

The `units` list in the same section decides which unit types count as heroes.

### Let the Eye of Kilrogg scout for you (or not)

Both switches are off by default.

```toml
[eye_of_kilrogg]
cast = true          # idle ogre-magi cast it by themselves
cast_at_mana = 255   # only at full mana, so Bloodlust always comes first. Lower it for more eyes.
max_active = 1       # eyes out at once
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

### Gold mines that never run dry

Off by default. It covers every mine on the map, the computer's too.

```toml
[gold_mines]
unlimited = true
```

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

Every cast is then written to `x86\gameplay_options.log` with the caster, the target and the map position.

## Every setting

| Section | Setting | Default | Meaning |
|---|---|---|---|
| general | enabled | true | Master switch for autocasting (same as the hotkey) |
| general | toggle_key | "F9" | Ctrl + key toggles autocast |
| general | interval_ticks | 10 | Game steps between autocast passes. Lower reacts faster |
| general | log_casts | false | Write every cast to gameplay_options.log |
| autocast | search_radius | 8 | Tiles a caster searches for targets |
| autocast | combat_radius | 6 | A unit counts as fighting when an enemy is this close to it |
| autocast | own_units_only | true | Friendly spells skip allies' units |
| autocast | cast_while_attacking | true | Casters may interrupt their own attack to cast |
| spells | heal, slow, bloodlust, raise_dead | true | One switch per spell |
| spells | exorcism, polymorph, death_coil, haste, unholy_armor | false | |
| heal | min_missing_hp | 10 | Heal only units missing at least this many HP |
| heal | below_percent | 100 | Also require HP at or below this percent. 100 = off |
| polymorph | targets | flyers, casters, big ground units | Valid targets in priority order |
| haste | flyers_only | true | Haste only your air units |
| heroes | units | the 15 campaign heroes | What counts as a hero |
| heroes | regen_hp_per_second | 0 | 0 = off |
| heroes | regen_for | "all" | "all" or "mine" |
| eye_of_kilrogg | cast | false | Idle ogre-magi cast Eye of Kilrogg |
| eye_of_kilrogg | cast_at_mana | 255 | Mana needed before casting it |
| eye_of_kilrogg | max_active | 1 | Eyes out at the same time |
| eye_of_kilrogg | auto_scout | false | Eyes fly to unexplored ground |
| gold_mines | unlimited | false | Mines never run dry (all mines) |
| oil_platforms | unlimited | false | Oil platforms never run dry (all platforms) |
| workers | auto_repair / repair_idle_seconds / repair_radius | true / 1 / 10 | Idle workers repair your damaged buildings |
| workers | auto_harvest / harvest_idle_seconds / harvest_radius | false / 10 / 5 | Idle workers go to the nearest mine or tree |
| health / costs / time | all | 1.0 | Master multiplier of the section: everything (units, ships, structures, research) |
| health / costs / time | units, structures | 1.0 | Every unit and ship / every structure, both races |
| costs / time | research | 1.0 | All research of both races |
| health.human / health.orc | all, units, the 8 unit groups, heroes; structures, buildings, building_upgrades | 1.0 each | Health by race, kind and group. Units cap 65535, structures 32767 |
| health.neutral | all | 1.0 | Skeletons, daemons, critters, Eye of Kilrogg |
| costs.human / costs.orc | all, units, structures, research, the 8 unit groups, buildings, building_upgrades, the research groups | 1.0 each | Price multipliers, caps 2550 / 65535 |
| time.human / time.orc | same keys as costs | 1.0 each | Training, construction, upgrade and research time, cap 255 |
| range | upgrade_bonus | 1 | Range added by Longbow / Lighter Axes |
| unit.NAME / building.NAME | hit_points, armor, basic_damage, piercing_damage, range, sight, gold, lumber, oil, build_time | -1 each | Base stats of one unit or structure type, -1 = the game's value |
