# Config tutorial: autocast.toml

All settings live in one text file: `Warcraft II Remastered\x86\autocast.toml`. Open it with Notepad.

## The three rules

1. **Save while playing.** The mod re-reads the file every couple of seconds. A banner in game says
   "Autocast: settings reloaded". No restart needed.
2. **Mistakes are safe.** If the file has a typo the mod keeps your previous settings, shows
   "autocast.toml has an error" in game, and writes the line number to `x86\autocast.log`. A misspelled setting name
   is reported in the log as "unknown key" instead of being silently ignored.
3. **Delete to reset.** Delete `autocast.toml` and the default file is written again the next time a game starts.

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

### Turn one spell off

```toml
[spells]
exorcism = false
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

```toml
[heroes]
regen_hp_per_second = 0     # off
# or
regen_hp_per_second = 2
regen_for = "mine"          # your heroes only; "all" includes enemy heroes
```

The `units` list in the same section decides which unit types count as heroes.

### Let the Eye of Kilrogg scout for you (or not)

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
auto_repair = true
repair_idle_seconds = 1
repair_radius = 10
auto_harvest = true
harvest_idle_seconds = 10
harvest_radius = 5       # raise this if your idle workers stand far from the trees
```

### Gold mines that never run dry

Off by default. It covers every mine on the map, the computer's too.

```toml
[gold_mines]
unlimited = true
```

### Health, prices and sight

These three sections change the game's unit data. They are read **when a new map starts** (new mission, custom game,
restart), not in the middle of a game, and a savegame keeps the numbers it was made with.

The health and price settings are multipliers: `1.0` is the game's own number (the default for all of them), `2.0`
doubles it, `0.5` halves it. Health multipliers only touch units, never structures. Slower, weightier fights with a
kinder economy:

```toml
[health]
units = 2.0        # every unit that is not a hero
heroes = 4.0       # the unit types listed under [heroes] units

[costs]
units = 0.5              # gold and lumber price of units
ranged_upgrades = 0.5    # elf / troll research
siege_upgrades = 0.5     # ballista and catapult upgrades

[vision]
dragon = 2               # unit_name = extra sight. Total sight tops out at 9
gryphon_rider = 2
```

Every price group in `[costs]`:

| Setting | What it prices |
|---|---|
| `units` | every unit: gold and lumber (oil is untouched) |
| `buildings` | structures your workers place: gold, lumber and oil |
| `building_upgrades` | keep / stronghold, castle / fortress, guard tower, cannon tower |
| `melee_upgrades` | swords, battle axes, shields |
| `ranged_upgrades` | arrows, throwing axes, ranger / berserker upgrade, longbow, lighter axes, scouting, marksmanship, regeneration |
| `siege_upgrades` | ballista and catapult upgrades |
| `paladin_ogre_mage_upgrades` | paladin and ogre-mage upgrade plus their spells: holy vision, healing, exorcism, eye of kilrogg, bloodlust, runes |
| `naval_upgrades` | ship cannons and ship armor |
| `mage_death_knight_spells` | mage and death knight spell research |

Expensive fortifications, cheap knights and paladins:

```toml
[costs]
buildings = 1.5
building_upgrades = 2.0
melee_upgrades = 0.5
paladin_ogre_mage_upgrades = 0.5
```

The engine's own limits cap the results:

| What | Limit | Why |
|---|---|---|
| Unit hit points | 65535 | stored in 16 bits. From 10000 up the status panel stops printing the numbers; the health bar still works |
| Unit or structure price | 2550, in steps of 10 | the game stores these prices as one byte of tens |
| Research price | 65535 | stored in 16 bits |

Any multiplier from 0.01 to 1000 is accepted. Something that costs anything never becomes free, and what is free stays free.

Give scouts better eyes too:

```toml
[vision]
dragon = 2
gryphon_rider = 2
flying_machine = 1
zeppelin = 1
```

Writing a `[vision]` section replaces the default list, so keep the lines you still want. An empty `[vision]` section
means no sight bonus for anyone.

Vanilla numbers, autocast only (health and prices are already 1.0 by default):

```toml
[vision]

[heroes]
regen_hp_per_second = 0

[workers]
auto_harvest = false
auto_repair = false
```

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

Every cast is then written to `x86\autocast.log` with the caster, the target and the map position.

## Every setting

| Section | Setting | Default | Meaning |
|---|---|---|---|
| general | enabled | true | Master switch for autocasting (same as the hotkey) |
| general | toggle_key | "F9" | Ctrl + key toggles autocast |
| general | interval_ticks | 10 | Game steps between autocast passes. Lower reacts faster |
| general | log_casts | false | Write every cast to autocast.log |
| autocast | search_radius | 8 | Tiles a caster searches for targets |
| autocast | combat_radius | 6 | A unit counts as fighting when an enemy is this close to it |
| autocast | own_units_only | true | Friendly spells skip allies' units |
| autocast | cast_while_attacking | true | Casters may interrupt their own attack to cast |
| spells | heal, exorcism, slow, polymorph, bloodlust, death_coil, haste, raise_dead | true | One switch per spell |
| spells | unholy_armor | false | |
| heal | min_missing_hp | 10 | Heal only units missing at least this many HP |
| heal | below_percent | 100 | Also require HP at or below this percent. 100 = off |
| polymorph | targets | flyers, casters, big ground units | Valid targets in priority order |
| haste | flyers_only | true | Haste only your air units |
| heroes | units | the 15 campaign heroes | What counts as a hero |
| heroes | regen_hp_per_second | 1 | 0 = off |
| heroes | regen_for | "all" | "all" or "mine" |
| eye_of_kilrogg | cast | true | Idle ogre-magi cast Eye of Kilrogg |
| eye_of_kilrogg | cast_at_mana | 255 | Mana needed before casting it |
| eye_of_kilrogg | max_active | 1 | Eyes out at the same time |
| eye_of_kilrogg | auto_scout | true | Eyes fly to unexplored ground |
| gold_mines | unlimited | false | Mines never run dry (all mines) |
| workers | auto_repair / repair_idle_seconds / repair_radius | true / 1 / 10 | Idle workers repair your damaged buildings |
| workers | auto_harvest / harvest_idle_seconds / harvest_radius | true / 10 / 5 | Idle workers go to the nearest mine or tree |
| health | units / heroes | 1.0 / 1.0 | Max HP multipliers for units (never structures), cap 65535, applied at map start |
| costs | units / buildings / building_upgrades | 1.0 each | Price multipliers for units, structures and structure upgrades, cap 2550, applied at map start |
| costs | melee_upgrades / ranged_upgrades / siege_upgrades / paladin_ogre_mage_upgrades / naval_upgrades / mage_death_knight_spells | 1.0 each | Research price multipliers by group, cap 65535, applied at map start |
| vision | unit_name = bonus | dragon 2, gryphon_rider 2 | Extra sight range, applied at map start |
