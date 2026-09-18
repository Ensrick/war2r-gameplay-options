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
